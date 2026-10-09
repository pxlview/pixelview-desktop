"""Portable (Windows/Linux) Qt transport against loopback servers.

Builds test/pixelview/websocket_qt_probe.cpp with the pinned macOS Qt; the code
under test is the same Qt/C++ that ships on Windows. No GUI/media/credentials.
Set PIXELVIEW_SLOW_TESTS=1 for the 40-second keepalive timeout case.
"""
import base64, hashlib, http.server, json, os, pathlib, socket, socketserver, ssl, struct, subprocess, tempfile, threading, time, unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
QT = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'
GUID = b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
SOURCES = ['test/pixelview/websocket_qt_probe.cpp', 'frontend/utility/PixelviewWebSocket.cpp',
           'frontend/utility/PixelviewDesktopQt.cpp', 'frontend/utility/PixelviewReceiverQt.cpp']


def frame(opcode, payload=b'', fin=True, mask=False):
    head = bytes([(0x80 if fin else 0) | opcode])
    n = len(payload)
    m = 0x80 if mask else 0
    if n < 126: head += bytes([m | n])
    elif n < 65536: head += bytes([m | 126]) + struct.pack('!H', n)
    else: head += bytes([m | 127]) + struct.pack('!Q', n)
    if mask:
        key = b'\x01\x02\x03\x04'
        payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
        head += key
    return head + payload


def read_frame(f):
    h = f.read(2)
    if len(h) < 2: return None
    assert h[1] & 0x80, 'client frames must be masked'
    n = h[1] & 127
    if n == 126: n = struct.unpack('!H', f.read(2))[0]
    elif n == 127: n = struct.unpack('!Q', f.read(8))[0]
    key = f.read(4)
    data = bytes(b ^ key[i % 4] for i, b in enumerate(f.read(n)))
    return h[0] & 0x0f, data


class Server:
    """Scenario chosen by request path; records what the client sent."""
    def __init__(self, tls_context=None):
        self.events = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'
            def log_message(self, *a): pass
            def reply(self, code, body=b'', headers=()):
                self.send_response(code)
                for k, v in headers: self.send_header(k, v)
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                outer.events.append(('post', self.path, body))
                if self.path == '/login': self.reply(200, b'{"client_token":"fixture"}')
                elif self.path == '/login-redirect': self.reply(302, headers=[('Location', 'http://127.0.0.1:1/steal')])
                elif self.path == '/login-big': self.reply(200, b'x' * 300000)
                else: self.reply(401, b'{"detail":"no"}')
            def do_GET(self):
                path = self.path.split('?')[0]
                outer.events.append(('get', self.path))
                if path in ('/401', '/desktop/401'): return self.reply(401)
                if path == '/403': return self.reply(403)
                if path == '/302': return self.reply(302, headers=[('Location', 'http://127.0.0.1:1/')])
                key = self.headers['Sec-WebSocket-Key'].encode()
                accept = base64.b64encode(hashlib.sha1(key + GUID).digest()).decode()
                if path == '/badaccept': accept = 'AAAA'
                self.send_response(101); self.send_header('Upgrade', 'websocket'); self.send_header('Connection', 'Upgrade')
                self.send_header('Sec-WebSocket-Accept', accept); self.end_headers(); self.wfile.flush()
                self.connection.settimeout(60)
                w = self.wfile
                def send(data): w.write(data); w.flush()
                try:
                    if path == '/echo': send(frame(1, b'hello'))
                    elif path == '/frag': send(frame(1, b'hel', fin=False) + frame(9, b'p') + frame(0, b'lo'))
                    elif path == '/big': send(frame(1, b'x' * 20000))
                    elif path == '/masked': send(frame(1, b'hi', mask=True))
                    elif path == '/binary': send(frame(2, b'\x00'))
                    elif path == '/badutf8': send(frame(1, b'\xff\xfe'))
                    elif path == '/close4000': send(frame(8, struct.pack('!H', 4000) + b'bye'))
                    elif path == '/drop': time.sleep(0.2); self.connection.shutdown(socket.SHUT_RDWR); return
                    elif path in ('/desktop/ws', '/desktop/close4403', '/desktop/pong2s'):
                        send(frame(1, json.dumps({'mutation': 'DESKTOP_READY', 'data': {}}).encode()))
                    elif path == '/wsocket': pass
                    while True:
                        got = read_frame(self.rfile)
                        if got is None: break
                        op, data = got
                        outer.events.append(('frame', path, op, data, time.monotonic()))
                        if op == 8: send(frame(8, data[:2])); break
                        if op == 9 and path == '/desktop/pong2s': time.sleep(2); send(frame(10, data))
                        elif op == 9 and path != '/keepalive': send(frame(10, data))
                        if op == 1 and path == '/desktop/close4403': send(frame(8, struct.pack('!H', 4403)))
                        if op == 1 and path == '/wsocket': send(frame(1, b'{"mutation":"SOCKET_ADD_VIEWER_WEB"}'))
                except (ConnectionResetError, BrokenPipeError, socket.timeout, OSError):
                    pass
                finally:
                    self.close_connection = True

        class Threaded(socketserver.ThreadingMixIn, http.server.HTTPServer):
            daemon_threads = True
        self.httpd = Threaded(('127.0.0.1', 0), Handler)
        if tls_context: self.httpd.socket = tls_context.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def frames(self, path):
        return [(e[2], e[3], e[4]) for e in self.events if e[0] == 'frame' and e[1] == path]


@unittest.skipUnless(QT.exists(), 'pinned Qt is not staged in .deps')
class PortableTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.probe = cls.tmp.name + '/probe'
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT), '-F' + str(QT),
                        '-framework', 'QtCore', '-framework', 'QtNetwork', '-Wl,-rpath,' + str(QT),
                        *[str(ROOT / s) for s in SOURCES], '-o', cls.probe], check=True)
        cls.server = Server()

    @classmethod
    def tearDownClass(cls):
        cls.server.httpd.shutdown(); cls.tmp.cleanup()

    def run_probe(self, *args, timeout=30):
        out = subprocess.run([self.probe, *args], capture_output=True, text=True, timeout=timeout)
        return [json.loads(line) for line in out.stdout.splitlines() if line.startswith('{')]

    def url(self, path, scheme='ws'):
        return f'{scheme}://127.0.0.1:{self.server.port}{path}'

    def closed(self, events):
        found = [e for e in events if e['event'] in ('closed', 'disconnected')]
        self.assertEqual(len(found), 1, events)
        return found[0]

    def test_message_echo_and_client_close(self):
        events = self.run_probe('ws', self.url('/echo'), 'echo', 'close-on-message')
        self.assertEqual([e['event'] for e in events][:3], ['opened', 'message', 'closing'], events)
        self.assertEqual(events[1]['data'], 'hello')
        time.sleep(0.3)
        frames = self.server.frames('/echo')
        self.assertEqual(frames[0][:2], (1, b'echo:hello'))
        self.assertEqual(frames[1][:2], (8, struct.pack('!H', 1000)))
        self.assertFalse(any(e['event'] == 'closed' for e in events), 'a local close must not report')

    def test_fragmented_message_with_interleaved_ping(self):
        events = self.run_probe('ws', self.url('/frag'), 'close-on-message')
        self.assertEqual(events[1]['data'], 'hello', events)
        time.sleep(0.3)
        self.assertIn((10, b'p'), [f[:2] for f in self.server.frames('/frag')])

    def test_protocol_violations(self):
        for path, code in [('/big', 1009), ('/masked', 1002), ('/binary', 1003), ('/badutf8', 1007)]:
            with self.subTest(path=path):
                closed = self.closed(self.run_probe('ws', self.url(path)))
                self.assertEqual((closed['code'], closed['http'], closed['tls']), (code, 0, False))
                time.sleep(0.2)
                self.assertIn((8, struct.pack('!H', code)), [f[:2] for f in self.server.frames(path)])

    def test_server_close_is_echoed_and_reported(self):
        closed = self.closed(self.run_probe('ws', self.url('/close4000')))
        self.assertEqual(closed['code'], 4000)
        time.sleep(0.2)
        self.assertEqual(self.server.frames('/close4000')[0][:2], (8, struct.pack('!H', 4000)))

    def test_abnormal_drop_is_1006(self):
        self.assertEqual(self.closed(self.run_probe('ws', self.url('/drop')))['code'], 1006)

    def test_refused_upgrades(self):
        for path, http in [('/401', 401), ('/403', 403), ('/302', 302)]:
            with self.subTest(path=path):
                closed = self.closed(self.run_probe('ws', self.url(path)))
                self.assertEqual((closed['code'], closed['http']), (0, http))
        closed = self.closed(self.run_probe('ws', self.url('/badaccept')))
        self.assertEqual((closed['code'], closed['http'], closed['tls']), (0, 0, False))

    def test_oversized_send_is_refused(self):
        events = self.run_probe('ws', self.url('/echo'), 'send-big', 'close-on-message')
        self.assertIn({'event': 'sent', 'ok': False}, events)

    def test_desktop_close_classification(self):
        events = self.run_probe('desktop', self.url('/desktop/401'))
        self.assertEqual(self.closed(events)['code'], 4401)
        self.assertEqual(self.closed(self.run_probe('desktop', self.url('/403')))['code'], 4403)
        self.assertEqual(self.closed(self.run_probe('desktop', self.url('/302')))['code'], 4403)
        events = self.run_probe('desktop', self.url('/desktop/close4403'))
        self.assertEqual(json.loads(events[0]['data'])['mutation'], 'DESKTOP_READY')
        self.assertEqual(self.closed(events)['code'], 4403)
        upgrade = [e[1] for e in self.server.events if e[0] == 'get' and e[1].startswith('/desktop/close4403')][-1]
        self.assertTrue(upgrade.endswith('?token=device-token'), upgrade)
        pong = self.server.frames('/desktop/close4403')[0]
        self.assertEqual(json.loads(pong[1])['message'], 'PONG_RESPONSE')

    def test_receiver_login_socket_and_cancel(self):
        events = self.run_probe('receiver', self.url('/login', 'http'), self.url('/wsocket'))
        self.assertEqual([e['event'] for e in events], ['login', 'opened', 'message', 'cancelled'], events)
        self.assertEqual(events[0]['code'], 200)
        time.sleep(0.3)
        frames = self.server.frames('/wsocket')
        self.assertEqual(frames[0][:2], (1, b'{"message":"ADD_VIEWER_WEB"}'))
        self.assertEqual(frames[1][:2], (8, struct.pack('!H', 1000)), 'cancel must send a normal close')

    def test_receiver_login_failures(self):
        for path, code in [('/login-redirect', 302), ('/login-big', 413), ('/login-denied', 401)]:
            with self.subTest(path=path):
                events = self.run_probe('receiver', self.url(path, 'http'), self.url('/wsocket'))
                self.assertEqual(events[0], {'event': 'login', 'code': code, 'size': 0 if code != 401 else 15}, events)
        self.assertFalse(any(e[0] == 'post' and e[1] == '/steal' for e in self.server.events), 'redirect was followed')
        self.assertEqual(self.closed(self.run_probe('receiver', self.url('/login', 'http'), self.url('/403')))['code'], 1008)

    def test_untrusted_tls_is_terminal(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=127.0.0.1',
                            '-keyout', tmp + '/k.pem', '-out', tmp + '/c.pem'], check=True, capture_output=True)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(tmp + '/c.pem', tmp + '/k.pem')
            server = Server(context)
            try:
                base = f'127.0.0.1:{server.port}'
                closed = self.closed(self.run_probe('ws', f'wss://{base}/echo'))
                self.assertEqual((closed['code'], closed['tls']), (0, True))
                self.assertEqual(self.closed(self.run_probe('desktop', f'wss://{base}/desktop/ws'))['code'], 4403)
                events = self.run_probe('receiver', f'https://{base}/login', f'wss://{base}/wsocket')
                self.assertEqual(events[0]['code'], 495, events)
                self.assertFalse(any(e[0] == 'post' for e in server.events), 'credentials crossed an untrusted TLS session')
            finally:
                server.httpd.shutdown()

    def test_desktop_control_round_trip(self):
        # Same scenario as the macOS native test: the first keepalive ping
        # (20 s after open) is answered 2 s late, and that is the round trip
        # the connection report sends; closing the socket forgets it.
        events = self.run_probe('desktop-rtt', self.url('/desktop/pong2s'), 'keepalive', timeout=40)
        rtt = [e for e in events if e['event'] == 'rtt']
        self.assertEqual(len(rtt), 1, events)
        self.assertTrue(1950 <= rtt[0]['ms'] < 2600, rtt)
        self.assertEqual([e['ms'] for e in events if e['event'] == 'reset'], [-1])

    @unittest.skipUnless(os.environ.get('PIXELVIEW_SLOW_TESTS') == '1', 'slow: set PIXELVIEW_SLOW_TESTS=1')
    def test_keepalive_pings_and_times_out(self):
        start = time.monotonic()
        closed = self.closed(self.run_probe('ws', self.url('/keepalive'), 'keepalive', timeout=70))
        elapsed = time.monotonic() - start
        self.assertEqual(closed['code'], 1006)
        pings = [f for f in self.server.frames('/keepalive') if f[0] == 9]
        self.assertEqual(len(pings), 1, pings)
        self.assertTrue(39 <= elapsed < 45, elapsed)


if __name__ == '__main__':
    unittest.main()
