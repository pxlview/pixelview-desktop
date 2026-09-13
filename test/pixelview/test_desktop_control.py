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
    payload = bytes(v ^ mask[i % 4] for i, v in enumerate(body))
    if header[0] & 15 == 9:
        send(sock, payload, 10)  # Answer native client pings; they are not application liveness.
        return frame(sock)
    return header[0] & 15, payload


def send(sock, body, opcode=1):
    if isinstance(body, dict):
        body = json.dumps(body, separators=(',', ':')).encode()
    length = bytes([len(body)]) if len(body) < 126 else b'\x7e' + len(body).to_bytes(2, 'big')
    sock.sendall(bytes([128 | opcode]) + length + body)


def mutation(name, data=None):
    return {'mutation': name, 'data': data or {}}


def handshake(sock):
    request = b''
    while b'\r\n\r\n' not in request:
        request += read(sock, 1)
    # The device token authenticates the upgrade; no first message follows.
    assert request.startswith(b'GET /desktop/ws?token=fixture-secret HTTP/1.1\r\n'), request[:80]
    key = next(x.split(b':', 1)[1].strip() for x in request.split(b'\r\n') if x.lower().startswith(b'sec-websocket-key:'))
    accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
    sock.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
    send(sock, mutation('DESKTOP_READY', {'desktop_id': 'fixture', 'node_id': 'fixture'}))


def expect(sock, name):
    while True:
        opcode, body = frame(sock)
        if opcode == 10:
            continue
        assert opcode == 1, opcode
        message = json.loads(body)
        assert message['message'] == name, (name, message)
        return message['data']


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


class DesktopControl(unittest.TestCase):
    def test_cancelled_delegate_sends_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_transport_cancel.mm')
            subprocess.run([binary], check=True, timeout=10)

    def test_control_pong_does_not_extend_ping_silence_watchdog(self):
        with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_control_silence.mm')
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(75)
            counts = {'pongs': 0, 'messages': 0}
            errors = []

            def serve():
                try:
                    sock, _ = listener.accept()
                    with sock:
                        sock.settimeout(70)
                        handshake(sock)
                        send(sock, b'keepalive', 9)
                        while True:
                            opcode, body = frame(sock)
                            if opcode == 8:
                                break
                            if opcode == 10:
                                counts['pongs'] += 1
                                send(sock, b'keepalive', 9)  # RFC keepalive only; no SOCKET_SEND_PING.
                            else:
                                counts['messages'] += 1
                except (EOFError, ConnectionResetError):
                    pass  # Production cancellation closes TCP immediately.
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            result = subprocess.run([binary, f'ws://127.0.0.1:{listener.getsockname()[1]}/desktop/ws'], timeout=80, capture_output=True, text=True)
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(errors)
            self.assertGreaterEqual(counts['pongs'], 2)
            self.assertEqual(counts['messages'], 0)
            self.assertFalse(thread.is_alive())
            print(result.stdout.strip())

    def run_scripted(self, mode, script, attempts, timeout=60):
        with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
            binary = compile_native(tmp, ROOT / 'test/pixelview/desktop_control.mm')
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(timeout)
            errors, log = [], []

            def serve():
                try:
                    for attempt in range(attempts):
                        sock, _ = listener.accept()
                        with sock:
                            sock.settimeout(25)
                            handshake(sock)
                            script(sock, attempt, log)
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            result = subprocess.run([binary, f'ws://127.0.0.1:{listener.getsockname()[1]}/desktop/ws', mode], timeout=timeout + 5, capture_output=True, text=True)
            thread.join(2)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(errors, errors)
            self.assertFalse(thread.is_alive())
            print(result.stdout.strip())
            return log

    def test_start_control_loss_reconnect_and_revocation(self):
        def script(sock, attempt, log):
            if attempt == 0:
                # The Desktop asks to start right after READY; the answer carries only WHIP config.
                self.assertEqual(expect(sock, 'DESKTOP_START'), {})
                port = sock.getsockname()[1]
                send(sock, mutation('DESKTOP_STARTED', {'config': {'whip': {'endpoint': f'http://127.0.0.1:{port}/whip', 'bearer_token': 'fixture-bearer'}, 'srt': {}}}))
            for index in range(3):
                send(sock, mutation('SOCKET_SEND_PING'))
                log.append((attempt, expect(sock, 'PONG_RESPONSE')))
                time.sleep(0.5)
            if attempt == 0:
                sock.shutdown(socket.SHUT_RDWR)  # Lost server/TCP while streaming, not a JSON error.
            else:
                send(sock, mutation('SOCKET_DESKTOP_REVOKED'))
                send(sock, (1000).to_bytes(2, 'big'), 8)  # The close after a terminal mutation may be normal.
                time.sleep(0.2)
        log = self.run_scripted('lifecycle', script, 2)
        self.assertEqual(log, [(a, {'streaming': True, 'settings': None}) for a in (0, 0, 0, 1, 1, 1)])

    def test_replaced_connection_keeps_media_and_does_not_reconnect(self):
        def script(sock, attempt, log):
            self.assertEqual(expect(sock, 'DESKTOP_START'), {})
            port = sock.getsockname()[1]
            send(sock, mutation('DESKTOP_STARTED', {'config': {'whip': {'endpoint': f'http://127.0.0.1:{port}/whip', 'bearer_token': 'fixture-bearer'}, 'srt': {}}}))
            for index in range(3):
                send(sock, mutation('SOCKET_SEND_PING'))
                log.append(expect(sock, 'PONG_RESPONSE')['streaming'])
            send(sock, mutation('SOCKET_DESKTOP_REPLACED'))
            send(sock, (1000).to_bytes(2, 'big'), 8)
            time.sleep(0.2)
        log = self.run_scripted('replaced', script, 1, timeout=20)
        self.assertEqual(log, [True, True, True])


if __name__ == '__main__':
    unittest.main()
