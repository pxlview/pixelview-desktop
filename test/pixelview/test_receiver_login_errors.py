"""Real loopback HTTP through compiled production controller/NSURLSession.
Optional live missing-session check: PIXELVIEW_TEST_LIVE_LOGIN_ORIGIN=http://127.0.0.1:8000
No credentials, database writes, viewer registration or service management.
"""
import http.server
import json
import os
import pathlib
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
QT = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
UNAUTHORIZED = 'Wrong session ID or password. Please try again. If the session was deleted, ask your host for a new link.'

class ReceiverLoginErrors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = cls.tmp.name + '/login-errors'
        subprocess.run(['clang++', '-std=c++17', '-fobjc-arc', '-fPIC', '-I'+str(ROOT),
                        '-F'+str(QT/'lib'), '-framework', 'QtCore', '-framework', 'Foundation',
                        '-Wl,-rpath,'+str(QT/'lib'), str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),
                        str(ROOT/'frontend/utility/PixelviewReceiverMac.mm'),
                        str(ROOT/'test/pixelview/receiver_login_errors.mm'), '-o', cls.binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def response(self, code, body, expected, retry=False):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        requests = []
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, format, *args): pass
            def do_POST(self):
                data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                requests.append((self.path, data['session_id'].startswith('missing-')))
                self.send_response(code)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                subprocess.run([self.binary, f'http://127.0.0.1:{server.server_port}', expected,
                                'retry' if retry else 'error'], check=True, timeout=12)
                self.assertEqual(requests, [('/login/player', True)])
            finally:
                server.shutdown()
                thread.join()

    def test_archived_including_soft_deleted_session(self):
        self.response(401, {'detail': 'Session archived'},
                      'The stream has ended. Thanks for watching! Ask your host for a new session link.')

    def test_unknown_or_missing_detail_is_safe_and_actionable(self):
        for code, body in [(401, {'detail': 'https://secret.invalid/?token=private'}),
                           (401, {}), (401, {'detail': ['Unauthorized']}),
                           (401, b'{broken'), (401, b'null'),
                           (404, {'detail': 'Not Found'}), (500, b'private traceback'),
                           (422, {'detail': [{'input': 'private-password'}]}),
                           (403, {'detail': 'Session archived'}),
                           (302, {'detail': 'Unauthorized'})]:
            with self.subTest(code=code, body_type=type(body).__name__):
                self.response(code, body,
                              'Receiver login is unavailable. Please try again later or contact your host.')

    def test_unexpected_success_response(self):
        for body in [b'', b'null', b'[]', b'{broken', {}, {'player': 'HLS'},
                     {'player': 'WHEP', 'client_token': 'private-token',
                      'stream_url': 'https://user:private@engine.invalid/whep'}]:
            with self.subTest(body_type=type(body).__name__):
                self.response(200, body,
                              'Invalid or unsupported receiver login response. Please try again later or contact your host.')

    def test_full(self):
        self.response(401, {'detail': 'Viewers limit reached'},
                      'Maximum viewers limit reached. Contact your host for more information.')

    @unittest.skipUnless(os.environ.get('PIXELVIEW_TEST_LIVE_LOGIN_ORIGIN'), 'live backend opt-in not set')
    def test_live_nonexistent_session(self):
        from urllib.parse import urlsplit
        origin = os.environ['PIXELVIEW_TEST_LIVE_LOGIN_ORIGIN']
        parsed = urlsplit(origin)
        self.assertEqual(parsed.scheme, 'http')
        self.assertIn(parsed.hostname, ('127.0.0.1', 'localhost', '::1'))
        subprocess.run([self.binary, origin, UNAUTHORIZED, 'live'], check=True, timeout=12)

    def test_retryable_http_keeps_reconnect_and_redacts_body(self):
        for code in [408, 429, 502, 503, 504]:
            with self.subTest(code=code):
                self.response(code, {'detail': 'private endpoint or password'},
                              'Receiver disconnected. Reconnecting…', retry=True)

    def test_oversized_error_body_remains_terminal(self):
        self.response(401, b'x'*262145,
                      'Receiver login is unavailable. Please try again later or contact your host.')

    def test_unauthorized_including_missing_or_deleted_session(self):
        self.response(401, {'detail': 'Unauthorized'}, UNAUTHORIZED)

if __name__ == '__main__':
    unittest.main()
