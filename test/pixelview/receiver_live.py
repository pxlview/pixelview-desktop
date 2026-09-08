"""Explicit opt-in existing-loopback backend smoke. Never starts media/server processes.
Usage: python3 test/pixelview/receiver_live.py /private/path/session.json
Input contains session_id/password. Neither is printed or passed on argv.
Read-only Redis correlation proves registration and live-record removal on stop.
Set PIXELVIEW_RECEIVER_SOAK_SECONDS=135 for uninterrupted long-lived control.
"""
import json, os, pathlib, socket, subprocess, sys, tempfile, time, urllib.request, uuid
ROOT=pathlib.Path(__file__).resolve().parents[2]
QT=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal'
def redis(*args):
    with socket.create_connection(('127.0.0.1',6379),timeout=3) as s:
        parts=[str(x).encode() for x in args]
        s.sendall(b'*'+str(len(parts)).encode()+b'\r\n'+b''.join(b'$'+str(len(x)).encode()+b'\r\n'+x+b'\r\n' for x in parts))
        f=s.makefile('rb')
        def read():
            line=f.readline(); kind=line[:1];value=line[1:-2]
            if kind==b'*':return [read() for _ in range(int(value))]
            if kind==b'$':
                n=int(value)
                if n<0:return None
                data=f.read(n);f.read(2);return data
            if kind==b':':return int(value)
            if kind==b'+':return value
            raise RuntimeError('Local Redis read failed')
        return read()
def viewer_keys(viewer_id):
    cursor='0';result=[]
    while True:
        cursor,keys=redis('SCAN',cursor,'MATCH','viewer:*','COUNT',100)
        for key in keys:
            if redis('HGET',key.decode(),'viewer_id')==viewer_id.encode():result.append(key.decode())
        cursor=cursor.decode()
        if cursor=='0':return result

def main():
    for url in ['http://127.0.0.1:8000/docs','http://127.0.0.1:8080/health']:
        with urllib.request.urlopen(url,timeout=5) as response:assert response.status==200
    assert redis('PING')==b'PONG'
    credentials=json.loads(pathlib.Path(sys.argv[1]).read_text())
    payload=json.dumps({key:credentials[key] for key in ['session_id','password']})+'\n';credentials.clear()
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['clang++','-std=c++17','-fobjc-arc','-fPIC','-I'+str(ROOT),'-F'+str(QT/'lib'),'-framework','QtCore','-framework','Foundation','-Wl,-rpath,'+str(QT/'lib'),str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),str(ROOT/'frontend/utility/PixelviewReceiverMac.mm'),str(ROOT/'test/pixelview/receiver_live.mm'),'-o',tmp+'/live'],check=True)
        p=subprocess.Popen([tmp+'/live'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        keys=[]
        try:
            p.stdin.write(payload);p.stdin.close();payload=''
            viewer=p.stdout.readline().strip()
            try:uuid.UUID(viewer)
            except ValueError:raise RuntimeError('Native local receiver authentication failed') from None
            keys=viewer_keys(viewer);assert len(keys)==1,'Native viewer not registered exactly once'
            print('Native player login + viewer registration verified (read-only Redis correlation)',flush=True)
            outcome=p.stdout.readline().strip();assert outcome=='STOPPED','Native heartbeat/control failed'
            assert p.wait(timeout=10)==0
            for _ in range(30):
                if all(redis('EXISTS',key)==0 for key in keys):break
                time.sleep(.1)
            assert all(redis('EXISTS',key)==0 for key in keys),'Viewer live record not removed'
            seconds=max(0,int(os.environ.get('PIXELVIEW_RECEIVER_SOAK_SECONDS','0'))) or 25
            print(f'{seconds}-second control connection + clean stop/live viewer removal verified',flush=True)
        finally:
            if p.poll() is None:p.terminate();p.wait(timeout=5)
if __name__=='__main__':main()
