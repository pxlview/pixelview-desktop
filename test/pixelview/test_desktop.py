"""Compile and execute real Qt Desktop protocol code, no OBS/hardware required."""
import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
class Desktop(unittest.TestCase):
    def test_network_and_keychain_native(self):
        impl = ROOT / 'frontend/utility/PixelviewDesktopMac.mm'
        self.assertTrue(impl.exists(), 'Native TLS WebSocket and Keychain implementation missing')
        with tempfile.TemporaryDirectory() as tmp:
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal'
            subprocess.run(['clang++','-std=c++17','-fobjc-arc','-fPIC','-I'+str(ROOT),'-F'+str(qt/'lib'),'-framework','QtCore','-framework','QtNetwork','-framework','Foundation','-framework','Security','-Wl,-rpath,'+str(qt/'lib'),str(impl),str(ROOT/'test/pixelview/desktop_transport.mm'),'-o',tmp+'/test'],check=True)
            import socket, threading, hashlib, base64, json
            with socket.socket() as server:
                server.bind(('127.0.0.1',0)); server.listen()
                errors=[]
                def serve():
                    try:
                        conn,_=server.accept()
                        with conn:
                            request=b''
                            while b'\r\n\r\n' not in request: request+=conn.recv(4096)
                            key=next(x.split(b':',1)[1].strip() for x in request.split(b'\r\n') if x.lower().startswith(b'sec-websocket-key:'))
                            accept=base64.b64encode(hashlib.sha1(key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                            conn.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
                            def read(n):
                                data=b''
                                while len(data)<n: data+=conn.recv(n-len(data))
                                return data
                            frame=read(2); length=frame[1]&127
                            mask=read(4); payload=read(length)
                            obj=json.loads(bytes(v^mask[i%4] for i,v in enumerate(payload)))
                            assert obj=={'type':'auth','device_token':'fixture-secret'}
                            body=b'{"type":"ready"}'
                            conn.sendall(bytes([129,len(body)])+body)
                            conn.recv(4096)
                            conn.sendall(bytes([136,2])+int(4401).to_bytes(2,'big'))
                            conn.recv(4096)
                    except Exception as e: errors.append(e)
                thread=threading.Thread(target=serve,daemon=True); thread.start()
                subprocess.run([tmp+'/test',f'ws://127.0.0.1:{server.getsockname()[1]}/desktop/ws'],check=True)
                thread.join(5); self.assertFalse(errors)
    def test_frontend_lease_gate_and_secret_boundary(self):
        streaming=(ROOT/'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        self.assertIn('RequestPixelviewStart()',streaming)
        self.assertIn('PixelviewLeaseValid()',streaming)
        self.assertIn('if (pixelviewLease.leased) PixelviewOutputStopped();',streaming)
        self.assertIn('!pixelviewDesktop && auth && auth->broadcastFlow()',streaming)
        service=(ROOT/'frontend/widgets/OBSBasic_Service.cpp').read_text()
        self.assertIn('pixelviewDesktop',service)
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        self.assertNotIn('CURLOPT_UNRESTRICTED_AUTH, 1L',whip)
        self.assertNotIn('CURLOPT_FOLLOWLOCATION, 1L',whip)
        self.assertNotIn('val.c_str()', '\n'.join(x for x in whip.splitlines() if 'do_log' in x))
        self.assertIn('pixelviewWhipSameOrigin(endpoint_url, resource_url)',whip)
        self.assertNotIn('err.what()',whip)
        self.assertNotIn('error_buffer[0] ?',whip)
        adv=(ROOT/'frontend/utility/AdvancedOutput.cpp').read_text()
        self.assertIn('strcmp(obs_service_get_type(service), "whip_custom")',adv)
        self.assertIn('pixelviewClosingSocket', (ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text())
        self.assertIn('WHIP resource origin rejected',whip)
        self.assertGreaterEqual(whip.count('pixelviewWhipSameOrigin(endpoint_url, resource_url)'),2)
        self.assertIn('resource_url.clear();',whip[whip.index('bool WHIPOutput::Init()'):whip.index('bool WHIPOutput::Init()')+300])
        main=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('pixelviewLease.fail("Stopping before shutdown.")',main)
        inc=(ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        self.assertIn('obs_output_active(outputHandler->streamOutput) || pixelviewStreamingBusy',inc)
        self.assertIn('pixelviewStreamingBusy=false; emit StreamingStopped();',inc)
    def test_pairing_category_and_local_unpair(self):
        main=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertLess(main.index('InitPixelviewDesktop(sidebar);'),main.index('pixelviewDevices = new QComboBox'))
        inc=(ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        self.assertIn('Pairing / Connection',inc)
        self.assertIn('pixelviewIdentityStatus',inc)
        self.assertIn('Unpair this installation',inc)
        self.assertIn('pixelviewUnpairPending',inc)
        stop=inc[inc.index('void OBSBasic::PixelviewOutputStopped()'):]
        self.assertLess(stop.index('obs_output_active'),stop.index('FinishPixelviewUnpair()'))
        self.assertLess(stop.index('pixelviewLease.outputStopped()'),stop.index('FinishPixelviewUnpair()'))
        self.assertIn('"NodeId"',inc); self.assertIn('"DesktopId"',inc)
        self.assertNotIn('/desktop/devices/',inc)
        self.assertIn('"PairingDisabled"',inc)
        ready=inc[inc.index('const bool wasReady'):inc.index('pixelviewDesktop->disconnected=')]
        self.assertLess(ready.index('pixelviewLease.receive'),ready.index('SavePixelviewIdentity()'))
        self.assertIn('Unpair this installation before changing backend origin.',inc)
        self.assertNotIn('if(pixelviewOrigin!=pixelviewDesktop->origin',inc)
        self.assertIn('"CleanupComplete"',inc)
        self.assertIn('bool saved=SavePixelviewIdentity()',inc)
        self.assertIn('Unpair incomplete: identity could not be saved. Do not restart; retry Unpair.',inc)
        self.assertNotIn('requested!=pixelviewOrigin && !pixelview::loadDevice',inc)
    def test_whip_resource_origin(self):
        header=ROOT/'plugins/obs-webrtc/pixelview-whip-security.h'
        self.assertTrue(header.exists(), 'WHIP resource credential boundary missing')
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'test.cpp'
            src.write_text('''#include "plugins/obs-webrtc/pixelview-whip-security.h"
#include <cassert>
int main(){
assert(pixelviewWhipSameOrigin("https://example.com/a","https://example.com:443/b?session=x"));
assert(!pixelviewWhipSameOrigin("https://example.com/a","http://example.com/b"));
assert(!pixelviewWhipSameOrigin("https://example.com/a","https://evil.com/b"));
assert(!pixelviewWhipSameOrigin("https://example.com/a","https://user:pass@example.com/b"));
assert(!pixelviewWhipSameOrigin("https://example.com/a","https://example.com/b#x"));
}''')
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT),str(src),'-lcurl','-o',tmp+'/test'],check=True)
            subprocess.run([tmp+'/test'],check=True)
    def test_real_whip_inactive_stop_completes(self):
        with tempfile.TemporaryDirectory() as tmp:
            frameworks=ROOT/'build_macos/libobs/RelWithDebInfo'
            deps=ROOT/'.deps/obs-deps-2026-08-26-universal'
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT/'libobs'),'-I'+str(ROOT/'build_macos/config'),'-I'+str(ROOT/'build_macos/libobs'),'-I'+str(deps/'include'),str(ROOT/'test/pixelview/desktop_whip_native.cpp'),'-F'+str(frameworks),'-framework','libobs','-Wl,-rpath,'+str(frameworks),'-Wl,-rpath,'+str(deps/'lib'),'-o',tmp+'/test'],check=True)
            plugin=ROOT/'build_macos/frontend/RelWithDebInfo/Pixelview.app/Contents/PlugIns/obs-webrtc.plugin/Contents/MacOS/obs-webrtc'
            subprocess.run([tmp+'/test',str(plugin),str(ROOT/'plugins/obs-webrtc/data')],check=True,timeout=8)
    def test_native_protocol(self):
        header = ROOT / 'frontend/utility/PixelviewDesktop.hpp'
        self.assertTrue(header.exists(), 'Native Desktop protocol is missing')
        with tempfile.TemporaryDirectory() as tmp:
            qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
            cmd = ['clang++', '-std=c++17', '-fPIC', '-I'+str(ROOT), '-F'+str(qt/'lib'), '-framework', 'QtCore', '-framework', 'QtNetwork', '-Wl,-rpath,'+str(qt/'lib'), str(ROOT/'test/pixelview/desktop_native.cpp'), '-o', tmp+'/test']
            subprocess.run(cmd, check=True)
            subprocess.run([tmp+'/test'], check=True)
