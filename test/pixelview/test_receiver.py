"""Compiled Qt receiver tests; independent of OBS, publishing and Keychain."""
import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
QT = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
class ReceiverTests(unittest.TestCase):
    def test_controller(self):
        self.assertTrue((ROOT/'frontend/utility/PixelviewReceiver.hpp').exists(), 'Receive controller missing')
        with tempfile.TemporaryDirectory() as tmp:
            cmd=['clang++','-std=c++17','-fPIC','-I'+str(ROOT),'-F'+str(QT/'lib'),'-framework','QtCore','-Wl,-rpath,'+str(QT/'lib'),str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),str(ROOT/'test/pixelview/receiver_native.cpp'),'-o',tmp+'/receiver']
            subprocess.run(cmd,check=True)
            subprocess.run([tmp+'/receiver'],check=True,timeout=15)
    def test_transport(self):
        self.assertTrue((ROOT/'frontend/utility/PixelviewReceiverMac.mm').exists(), 'Native receiver transport missing')
        import http.server, threading, json, base64, hashlib, struct, time
        errors=[]; received=[]
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            mode='ok'
            def log_message(self,*args): pass
            def do_POST(self):
                try:
                    data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                    assert data['session_id']=='fixture-session' and data['password']=='fixture-password'
                    assert self.path=='/login/player'
                    body=json.dumps({'player':'WHEP','client_token':'fixture-token','stream_url':'http://localhost:8080/server-chosen/whep?token=fixture'}).encode()
                    if Handler.mode=='oversized':body=b'x'*262145
                    if Handler.mode=='badjson':body=b'{broken json'
                    self.send_response(302 if Handler.mode=='redirect' else 401 if Handler.mode=='unauthorized' else 200)
                    if Handler.mode=='redirect': self.send_header('Location',f'http://127.0.0.1:{self.server.server_port}/stolen')
                    self.send_header('Content-Length',str(len(body))); self.end_headers();self.wfile.write(body)
                except Exception as e: errors.append(e)
            def do_GET(self):
                try:
                    assert self.path=='/wsocket?token=fixture-token', 'Redirect or unexpected WS path'
                    if Handler.mode=='wsredirect':
                        self.send_response(302);self.send_header('Location',f'http://127.0.0.1:{self.server.server_port}/stolen');self.send_header('Content-Length','0');self.end_headers();return
                    if Handler.mode=='ws403':
                        self.send_response(403);self.send_header('Content-Length','0');self.end_headers();return
                    key=self.headers['Sec-WebSocket-Key']
                    accept=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
                    self.send_response(101); self.send_header('Upgrade','websocket');self.send_header('Connection','Upgrade');self.send_header('Sec-WebSocket-Accept',accept);self.end_headers()
                    self.connection.settimeout(8)
                    def frame():
                        h=self.rfile.read(2)
                        assert len(h)==2
                        n=h[1]&127
                        if n==126:n=struct.unpack('!H',self.rfile.read(2))[0]
                        assert n<16384 and h[1]&128
                        mask=self.rfile.read(4);b=self.rfile.read(n)
                        return h[0]&15,bytes(x^mask[i%4] for i,x in enumerate(b))
                    def send(obj):
                        b=json.dumps(obj,separators=(',',':')).encode();self.wfile.write(bytes([129,len(b)])+b);self.wfile.flush()
                    op,b=frame();data=json.loads(b);assert data['message']=='ADD_VIEWER_WEB';assert data['data']['viewer_id'];received.append('register')
                    send({'mutation':'SOCKET_ADD_VIEWER_WEB','data':{'status':'success'}})
                    send({'mutation':'SOCKET_SEND_PING','data':{}})
                    op,b=frame();assert json.loads(b)['message']=='PONG_RESPONSE';received.append('pong')
                    if Handler.mode=='soak':
                        for _ in range(7):
                            time.sleep(5)
                            send({'mutation':'SOCKET_SEND_PING','data':{}})
                            op,b=frame();assert op==1 and json.loads(b)['message']=='PONG_RESPONSE';received.append('pong')
                    op,b=frame();assert op==8; received.append('close')
                    self.wfile.write(bytes([136,len(b)])+b);self.wfile.flush()
                    self.close_connection=True
                except Exception as e: errors.append(e)
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['clang++','-std=c++17','-fobjc-arc','-fPIC','-I'+str(ROOT),'-F'+str(QT/'lib'),'-framework','QtCore','-framework','Foundation','-Wl,-rpath,'+str(QT/'lib'),str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),str(ROOT/'frontend/utility/PixelviewReceiverMac.mm'),str(ROOT/'test/pixelview/receiver_transport.mm'),'-o',tmp+'/transport'],check=True)
            with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
                thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
                try:
                    url=f'http://127.0.0.1:{server.server_port}'
                    subprocess.run([tmp+'/transport',url],check=True,timeout=15)
                    self.assertEqual(received,['register','pong','close'])
                    Handler.mode='soak';received.clear()
                    subprocess.run([tmp+'/transport',url,'soak'],check=True,timeout=45)
                    self.assertEqual(received,['register']+['pong']*8+['close'])
                    for mode in ['redirect','unauthorized','badjson','ws403','oversized','wsredirect']:
                        Handler.mode=mode;subprocess.run([tmp+'/transport',url,'reject'],check=True,timeout=15)
                    self.assertFalse(errors,repr(errors))
                finally:server.shutdown();thread.join()
if __name__ == '__main__': unittest.main()
