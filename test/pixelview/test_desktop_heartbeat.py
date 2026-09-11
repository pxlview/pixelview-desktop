"""Real native WS over loopback. No Keychain calls, backend or app processes."""
import base64
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(sock, count):
    data = b''
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError('peer disconnected')
        data += chunk
    return data


def frame(sock):
    header = read(sock, 2)
    size = header[1] & 127
    if size == 126:
        size = int.from_bytes(read(sock, 2), 'big')
    elif size == 127:
        size = int.from_bytes(read(sock, 8), 'big')
    assert header[1] & 128, 'native client must mask frames'
    mask = read(sock, 4)
    body = read(sock, size)
    return header[0] & 15, bytes(v ^ mask[i % 4] for i, v in enumerate(body))


def send(sock, body, opcode=1):
    if isinstance(body, dict):
        body = json.dumps(body, separators=(',', ':')).encode()
    length = bytes([len(body)]) if len(body) < 126 else b'\x7e' + len(body).to_bytes(2, 'big')
    sock.sendall(bytes([128 | opcode]) + length + body)


def handshake(sock):
    request = b''
    while b'\r\n\r\n' not in request:
        request += read(sock, 1)
    assert request.startswith(b'GET /desktop/ws HTTP/1.1\r\n')
    key = next(x.split(b':', 1)[1].strip() for x in request.split(b'\r\n') if x.lower().startswith(b'sec-websocket-key:'))
    accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
    sock.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
    opcode, body = frame(sock)
    assert opcode == 1 and json.loads(body) == {'type': 'auth', 'device_token': 'fixture-secret'}
    send(sock, {'type': 'ready', 'desktop_id': 'fixture', 'node_id': 'fixture', 'heartbeat_interval': 15, 'lease_seconds': 45})


def compile_native(tmp, harness):
    # Compile production transport unchanged, exclude storage implementation so no
    # real secure-store operation can run even if the harness regresses.
    source = (ROOT / 'frontend/utility/PixelviewDesktopMac.mm').read_text()
    transport = Path(tmp) / 'transport.mm'
    transport.write_text(source[:source.index('static NSMutableDictionary *query')].removesuffix('namespace pixelview {\n'))
    qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'
    binary = str(Path(tmp) / 'native')
    subprocess.run(['clang++', '-std=c++17', '-fobjc-arc', '-fPIC', '-I' + str(ROOT), '-I' + str(ROOT / 'frontend/utility'), '-F' + str(qt), '-framework', 'QtCore', '-framework', 'QtNetwork', '-framework', 'Foundation', '-framework', 'Security', '-Wl,-rpath,' + str(qt), str(transport), str(harness), '-o', binary], check=True)
    return binary


class DesktopHeartbeat(unittest.TestCase):
    def test_cancelled_delegate_cannot_authenticate(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_transport_cancel.mm')
            subprocess.run([binary], check=True, timeout=10)

    def test_control_pong_does_not_extend_application_deadline(self):
        with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_heartbeat_timeout.mm')
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(40)
            counts = {'pongs': 0, 'heartbeats': 0}
            errors = []

            def serve():
                try:
                    sock, _ = listener.accept()
                    with sock:
                        sock.settimeout(35)
                        handshake(sock)
                        send(sock, b'keepalive', 9)
                        while True:
                            opcode, body = frame(sock)
                            if opcode == 8:
                                break
                            if opcode == 10:
                                counts['pongs'] += 1
                            else:
                                assert opcode == 1 and json.loads(body)['type'] == 'heartbeat'
                                counts['heartbeats'] += 1
                                send(sock, b'keepalive', 9)  # Intentionally no application ACK.
                except (EOFError, ConnectionResetError):
                    pass  # Production cancellation closes TCP immediately.
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            result = subprocess.run([binary, f'ws://127.0.0.1:{listener.getsockname()[1]}/desktop/ws'], timeout=40, capture_output=True, text=True)
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(errors)
            self.assertGreaterEqual(counts['pongs'], 2)
            self.assertEqual(counts['heartbeats'], 1)
            self.assertFalse(thread.is_alive())
            print(result.stdout.strip())

    def test_real_heartbeat_drop_reauthentication_and_revocation(self):
        with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_heartbeat.mm')
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(95)
            errors = []
            counts = []

            def serve():
                try:
                    for attempt in range(2):
                        sock, _ = listener.accept()
                        with sock:
                            sock.settimeout(25)
                            handshake(sock)
                            start = time.monotonic()
                            count = 0
                            # No application server PING: Desktop sends heartbeats.
                            while count < (4 if attempt == 0 else 1):
                                opcode, body = frame(sock)
                                if opcode == 10:
                                    continue
                                assert opcode == 1 and json.loads(body) == {'type': 'heartbeat', 'streaming': False}
                                count += 1
                                send(sock, {'type': 'heartbeat', 'lease_expires_at': None})
                                send(sock, b'control', 9)  # RFC6455 auto-pong, independent of lease ACK.
                            counts.append(count)
                            if attempt == 0:
                                time.sleep(max(0, 65.1 - (time.monotonic() - start)))
                                sock.shutdown(socket.SHUT_RDWR)  # Lost server/TCP, not a fake JSON error.
                            else:
                                send(sock, (4401).to_bytes(2, 'big'), 8)
                                time.sleep(0.2)
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            result = subprocess.run([binary, f'ws://127.0.0.1:{listener.getsockname()[1]}/desktop/ws'], timeout=100, capture_output=True, text=True)
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(errors)
            self.assertEqual(counts, [4, 1])
            self.assertFalse(thread.is_alive())
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
