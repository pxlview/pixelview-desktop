"""Real native transport liveness. Loopback only; no GUI/media/Keychain access."""
import concurrent.futures
import base64, hashlib, http.server, json, pathlib, struct, subprocess, tempfile, threading, time, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
QT=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
class ControlSocket(unittest.TestCase):
 def test_ping_policy(self):
  with tempfile.TemporaryDirectory() as tmp:
   subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(ROOT/'test/pixelview/control_ping_native.cpp'),'-o',tmp+'/test'],check=True)
   subprocess.run([tmp+'/test'],check=True)
 def test_native_timeout(self):
  errors=[]; pings=[]; auth=[]
  class Handler(http.server.BaseHTTPRequestHandler):
   protocol_version='HTTP/1.1'
   reject=False
   delayed=False
   def log_message(self,*args): pass
   def do_POST(self):
    data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
    assert data['client_type']=='pixelview-desktop'
    body=b'{"player":"WHEP","client_token":"fixture","stream_url":"http://localhost:8080/whep?token=fixture"}'
    self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
   def do_GET(self):
    try:
     if Handler.reject:
      self.send_response(403);self.send_header('Content-Length','0');self.end_headers();return
     key=self.headers['Sec-WebSocket-Key']
     accept=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
     self.send_response(101);self.send_header('Upgrade','websocket');self.send_header('Connection','Upgrade');self.send_header('Sec-WebSocket-Accept',accept);self.end_headers()
     self.connection.settimeout(25)
     opened=time.monotonic(); last_ping=opened
     ping_count=0
     while True:
      h=self.rfile.read(2)
      if not h: break
      n=h[1]&127
      if n==126:n=struct.unpack('!H',self.rfile.read(2))[0]
      mask=self.rfile.read(4);b=self.rfile.read(n);b=bytes(x^mask[i%4] for i,x in enumerate(b))
      op=h[0]&15
      if op==8:break
      if op==9:
       now=time.monotonic()
       assert 19.8 <= now-last_ping < 23, (self.path, "RFC ping interval", now-last_ping)
       last_ping=now
       pings.append(self.path);ping_count+=1
       if Handler.delayed and ping_count==1:
        time.sleep(2);self.wfile.write(bytes([138,len(b)])+b);self.wfile.flush()
       continue # Next ping is deliberately blackholed.
      data=json.loads(b)
      if self.path=='/desktop/ws':
       auth.append(data);reply={'type':'ready'}
      else:
       assert data['message']=='ADD_VIEWER_WEB' and data['data']['client_type']=='pixelview-desktop'
       reply={'mutation':'SOCKET_ADD_VIEWER_WEB','data':{'status':'success'}}
      body=json.dumps(reply,separators=(',',':')).encode();self.wfile.write(bytes([129,len(body)])+body);self.wfile.flush()
    except (ConnectionResetError,BrokenPipeError):pass
    except Exception as e:errors.append(repr(e))
    finally:self.close_connection=True
  with tempfile.TemporaryDirectory() as tmp:
   cmd=['clang++','-std=c++17','-fobjc-arc','-fPIC','-I'+str(ROOT),'-F'+str(QT)]
   for framework in ['QtCore','QtNetwork','Foundation','Security']:cmd+=['-framework',framework]
   cmd+=['-Wl,-rpath,'+str(QT)]
   cmd += [str(ROOT/p) for p in ['frontend/utility/PixelviewReceiver.cpp','frontend/utility/PixelviewReceiverMac.mm','frontend/utility/PixelviewDesktopMac.mm','test/pixelview/control_socket_native.mm']]
   subprocess.run(cmd+['-o',tmp+'/test'],check=True)
   with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
     def run(mode):
      subprocess.run([tmp+'/test',f'http://127.0.0.1:{server.server_port}',mode],check=True,timeout=70)
     # Independent native processes share only the loopback fixture.
     with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
      list(pool.map(run, ['sender','receiver']))
      Handler.delayed=True
      list(pool.map(run, ['sender-delay','receiver-delay']))
     Handler.reject=True
     for mode in ['sender-denied','receiver-denied']:
      subprocess.run([tmp+'/test',f'http://127.0.0.1:{server.server_port}',mode],check=True,timeout=18)
    finally:server.shutdown();thread.join()
   self.assertEqual(auth,[{'type':'auth','device_token':'fixture','resume_fence':17}]*2)
   self.assertEqual(len(pings),6)
   self.assertFalse(errors,errors)
if __name__=='__main__':unittest.main()
