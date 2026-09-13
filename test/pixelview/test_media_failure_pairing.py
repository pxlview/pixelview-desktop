"""Media failure must not disconnect authenticated control or disable manual retry.

Reuses the compiled real-controller/frontend drain harness. All IO boundaries are
in-memory fixtures: no app startup, credentials, Keychain, network or capture.
"""
import pathlib
import subprocess
import unittest

import test_desktop_retry

ROOT = pathlib.Path(__file__).resolve().parents[2]


class MediaFailurePairing(unittest.TestCase):
    def test_endpoint_rejection_uses_streaming_advice(self):
        streaming = (ROOT / 'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        start = streaming.index('\tswitch (code)', streaming.index('void OBSBasic::StreamingStop('))
        switch = streaming[start:streaming.index('\n\tif (use_last_error', start)]
        test_desktop_retry.DesktopRetry().compile_run('''
#include "libobs/obs-defs.h"
#include <string>
#include <cassert>
const char *Str(const char *s){return s;}
int main(){
 bool pixelviewDesktop=true, use_last_error=false, encode_error=false;
 int code=OBS_OUTPUT_INVALID_STREAM;const char *errorDescription="";
''' + switch + '''
 (void)pixelviewDesktop;(void)use_last_error;(void)encode_error;
 const std::string text=errorDescription;
 assert(text.find("streaming server")!=std::string::npos);
 assert(text.find("unavailable")!=std::string::npos);
 assert(text.find("rejected")!=std::string::npos);
 assert(text.find("admin")!=std::string::npos);
}
''')

    def test_terminal_media_failure_keeps_control_connected_and_manual_start_usable(self):
        desktop = (ROOT / 'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        streaming = (ROOT / 'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        # Execute the actual stop producer, including its duplicate-attempt guard;
        # stop before unrelated upstream popup/statusbar rendering.
        start = streaming.index('\tif (pixelviewDesktop) {', streaming.index('void OBSBasic::StreamingStop('))
        dispatch = streaming[start:streaming.index('\n\tconst char *errorDescription', start)]
        # Execute the exact production node/connection label policy too. The
        # existing fixture replaces widget rendering only, not the state policy.
        start = desktop.index(' pixelviewIdentityStatus->setText(paired ?')
        identity = desktop[start:desktop.index('\n // Mid', start)]

        setup_start = streaming.index("void OBSBasic::DisplayStreamStartError()")
        setup_body = streaming[setup_start:streaming.index("\nvoid OBSBasic::StartStreaming", setup_start)]
        runner = test_desktop_retry.DesktopRetry()
        compile_run = runner.compile_run

        def regression_source(source):
            source = '#include "libobs/obs-defs.h"\n' + source
            source = source.replace('#define QTStr(x) QStringLiteral(x)', '#define QTStr(x) QString::fromUtf8(x)')
            source = source.replace('struct Handler {', 'struct Handler {std::string lastError;')
            source = source.replace('class OBSBasic {', 'struct QMessageBox {template<class T> static void critical(T *w,QString,QString){assert(!w->button.enabled && !w->sysTrayStream->enabled);}};\nclass OBSBasic {')
            source = source.replace(
                'void RefreshPixelviewPairing(){}',
                'void RefreshPixelviewPairing(){const bool paired=pixelviewPairingDurable;'
                + identity + '}')
            source = source.replace('void bind();',
                                    'void bind(); void MediaStopped(int code){' + dispatch + '}')
            source = source.replace('void DisplayStreamStartError(){pixelviewLease.fail("setup error");}', setup_body.replace('OBSBasic::', ''))
            source = source[:source.index('int main() {')] + r'''
int main() {
 const QByteArray READY=R"({"mutation":"DESKTOP_READY","data":{"node_id":"fixture-node","desktop_id":"fixture-desktop"}})";
 const QByteArray PING=R"({"mutation":"SOCKET_SEND_PING","data":{}})";
 const QByteArray STARTED=R"({"mutation":"DESKTOP_STARTED","data":{"config":{"whip":{"endpoint":"https://fixture.invalid/whip","bearer_token":"synthetic-test-only"},"srt":{}}}})";
 for(bool setupFailure : {false,true}) {
 OBSBasic w;w.bind();Output output;w.handler.streamOutput=&output;
 Tray tray;w.sysTrayStream=&tray;
 std::vector<QString> sent; std::vector<bool> pongs;
 auto send=w.pixelviewLease.send;
 w.pixelviewLease.send=[&](QJsonObject o){sent.push_back(o["message"].toString());if(o["message"]=="PONG_RESPONSE") pongs.push_back(o["data"].toObject()["streaming"].toBool());send(o);};
 // Accepted ready is delivered by the in-memory authenticated control socket.
 // No disconnect/error callback is delivered anywhere in this scenario.
 w.connection.message(READY);
 assert(w.pixelviewPairingDurable && w.pixelviewLease.ready);
 assert(w.button.enabled && tray.enabled);
 assert(w.pixelviewLease.requestStart(0));
 w.connection.message(STARTED);
 assert(w.pixelviewLease.authorized(0));
 const auto failedGeneration=w.pixelviewLease.generation;
 w.pixelviewNativeAttempt=true;w.pixelviewStreamingBusy=true;
 // Native media start rejection has completed, so the output is inactive.
 // This is the code that produces the channel/stream-key popup upstream.
 if(setupFailure) w.MakeSetup()(false);else w.MediaStopped(OBS_OUTPUT_INVALID_STREAM);
 assert(!w.pixelviewNativeAttempt && !w.pixelviewLease.intent);
 assert(!w.pixelviewLease.acceptSetup(failedGeneration,0));
 assert(!w.button.enabled && !tray.enabled && w.pixelviewLease.ready);
 assert(!w.pixelviewLease.authorized(0));
 std::promise<void> setup;w.setupStreamingGuard=setup.get_future().share();
 auto staleSetup=w.MakeSetup();
 w.MediaStopped(OBS_OUTPUT_INVALID_STREAM); // Duplicate native stop is ignored.
 w.connection.message(STARTED); // Queued stale grant cannot revive or disconnect.
 Timer::run();
 assert(w.pixelviewStopPending && w.pixelviewLease.ready && w.connection.closes==0);
 assert(std::count(sent.begin(),sent.end(),QString("DESKTOP_STOP"))==0);
 w.connection.message(PING); // Server liveness continues through the drain and reports no stream.
 assert(pongs.size()==1 && !pongs[0] && w.pixelviewLease.ready && !w.pixelviewLease.authorized(0));
 staleSetup(true);assert(w.handler.starts==0);
 setup.set_value();
 Timer::run();
 assert(!w.pixelviewLease.started && !w.pixelviewLease.pending);
 assert(!w.pixelviewStopPending && !w.pixelviewStreamingBusy);
 assert(output.forceStops==0 && w.service==&w.oldService);
 assert(w.pixelviewPairingDurable && w.pixelviewIdentity.nodeId=="fixture-node");
 assert(!w.pixelviewLease.authorized(0)); // Never retain failed ingest authority.
 const auto failureDetail=w.label.text;
 w.RefreshPixelviewReconnect();
 assert(w.label.text==failureDetail);
 std::cerr << "after media failure: identity=" << w.identityLabel.text.toStdString()
           << ", ready=" << w.pixelviewLease.ready << ", socket closes=" << w.connection.closes
           << ", Start=" << w.button.enabled
           << ", tray Start=" << tray.enabled << ", reconnectAt=" << w.pixelviewReconnectAt
           << ", retryAt=" << w.pixelviewLease.retryAt << ", status=" << w.label.text.toStdString() << '\n';
 const bool connectedRetry = w.connection.closes==0 && w.pixelviewLease.ready &&
     w.identityLabel.text=="Node fixture-node · Connected" && w.button.enabled && tray.enabled;
 assert(connectedRetry && "media rejection must preserve authenticated control and usable manual Start");
 assert(std::count(sent.begin(),sent.end(),QString("DESKTOP_STOP"))==1);
 assert(w.pixelviewLease.requestStart(0));
 assert(w.pixelviewLease.pending && !w.pixelviewLease.authorized(0));
 assert(std::count(sent.begin(),sent.end(),QString("DESKTOP_START"))==2);
 assert(!w.pixelviewLease.acceptSetup(failedGeneration,0));
 Timer::run();
 }
 // Control revocation or ping silence must still fail closed during a media drain.
 for(bool revoked : {false,true}) {
  OBSBasic guard;guard.bind();guard.pixelviewLease.ready=true;
  guard.connection.authorizedToken="synthetic-retained-token";
  guard.pixelviewIdentity.accept({{"node_id","retained-node"},{"desktop_id","retained-desktop"}});
  assert(guard.pixelviewLease.requestStart(0));guard.pixelviewLease.pending=false;guard.pixelviewLease.started=true;
  guard.pixelviewNativeAttempt=true;
  std::promise<void> pending;guard.setupStreamingGuard=pending.get_future().share();
  guard.MediaStopped(OBS_OUTPUT_INVALID_STREAM);
  if(revoked) guard.connection.disconnected(4401);
  else {guard.pixelviewLease.pingDeadline=30000;guard.pixelviewClock.now=30000;guard.Watchdog();}
  assert(!guard.pixelviewLease.ready && !guard.pixelviewLease.intent);
  Timer::run();assert(guard.pixelviewStopPending && guard.connection.closes==0);
  pending.set_value();Timer::run();
  assert(!guard.pixelviewStopPending && guard.connection.closes==1 && !guard.button.enabled);
  if(revoked) {
   assert(guard.unpairs==0 && guard.pixelviewReconnectAt==0);
   assert(!guard.pixelviewPairingDurable && guard.pixelviewUnpairRetry && !guard.pixelviewUnpairPending);
   assert(config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled"));
   assert(guard.connection.authorizedToken.isEmpty());
   assert(guard.pixelviewIdentity.nodeId=="retained-node" && guard.pixelviewIdentity.desktopId=="retained-desktop");
   guard.ConnectPixelviewDesktop();assert(guard.authentications==0);
   application.config.booleans.clear(); // Remaining scenarios are independent installations.
  } else assert(guard.connection.authorizedToken=="synthetic-retained-token" && guard.pixelviewPairingDurable && guard.pixelviewReconnectAt>0);
 }
 // A stop release observes cleared authority immediately; DESKTOP_STOPPED needs no wait.
 pixelview::Desktop ack;ack.ready=true;
 assert(ack.requestStart(0));ack.pending=false;ack.started=true;
 ack.send=[&](QJsonObject o){assert(o["message"]=="DESKTOP_STOP");assert(!ack.started);ack.receive({{"mutation","DESKTOP_STOPPED"},{"data",QJsonObject{}}},0);};
 ack.mediaStopped("Streaming failed");ack.outputStopped();
 assert(ack.ready && !ack.started && !ack.pending);
 // Healthy server pings after a media failure keep the socket and never revive intent.
 OBSBasic quiet;quiet.bind();quiet.pixelviewLease.ready=true;
 assert(quiet.pixelviewLease.requestStart(0));
 quiet.pixelviewLease.pending=false;quiet.pixelviewLease.started=true;
 quiet.pixelviewNativeAttempt=true;quiet.MediaStopped(OBS_OUTPUT_INVALID_STREAM);Timer::run();
 quiet.pixelviewClock.now=20000;quiet.connection.message(PING);
 quiet.pixelviewClock.now=50000;quiet.Watchdog();Timer::run();
 assert(quiet.pixelviewLease.ready && !quiet.pixelviewLease.intent && quiet.connection.closes==0);
 assert(quiet.pixelviewReconnectAt==0);
}
'''
            try:
                compile_run(source)
            except subprocess.CalledProcessError as error:
                if error.returncode < 0:
                    self.fail('Compiled media/control regression assertion failed; see native diagnostic above')
                raise

        runner.compile_run = regression_source
        runner.test_compiled_frontend_drain()


if __name__ == '__main__':
    unittest.main()
