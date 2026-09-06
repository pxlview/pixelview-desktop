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
 for(bool setupFailure : {false,true}) {
 OBSBasic w;w.bind();Output output;w.handler.streamOutput=&output;
 Tray tray;w.sysTrayStream=&tray;
 std::vector<QString> sent;
 w.pixelviewLease.send=[&](QJsonObject o){sent.push_back(o["type"].toString());};
 // Accepted ready is delivered by the in-memory authenticated control socket.
 // No disconnect/error callback is delivered anywhere in this scenario.
 w.connection.message(R"({"type":"ready","node_id":"fixture-node","desktop_id":"fixture-desktop","heartbeat_interval":15,"lease_seconds":45})");
 assert(w.pixelviewPairingDurable && w.pixelviewLease.ready && w.heartbeat.active);
 assert(w.button.enabled && tray.enabled);
 assert(w.pixelviewLease.requestStart(0));
 w.connection.message(R"({"type":"started","fence":1,"lease_expires_at":45000,"config":{"whip":{"endpoint":"https://fixture.invalid/whip","bearer_token":"synthetic-test-only"}}})");
 assert(w.pixelviewLease.authorized(0));
 const auto failedGeneration=w.pixelviewLease.generation;
 w.pixelviewNativeAttempt=true;w.pixelviewStreamingBusy=true;
 // Native media start rejection has completed, so the output is inactive.
 // This is the code that produces the channel/stream-key popup upstream.
 if(setupFailure) w.MakeSetup()(false);else w.MediaStopped(OBS_OUTPUT_INVALID_STREAM);
 assert(!w.pixelviewNativeAttempt && !w.pixelviewLease.intent);
 assert(!w.pixelviewLease.acceptSetup(failedGeneration,0));
 assert(!w.button.enabled && !tray.enabled && w.heartbeat.active);
 assert(!w.pixelviewLease.authorized(0));
 std::promise<void> setup;w.setupStreamingGuard=setup.get_future().share();
 auto staleSetup=w.MakeSetup();
 w.MediaStopped(OBS_OUTPUT_INVALID_STREAM); // Duplicate native stop is ignored.
 w.connection.message(R"({"type":"started","fence":1})"); // Queued stale grant cannot revive or disconnect.
 Timer::run();
 assert(w.pixelviewStopPending && w.pixelviewLease.ready && w.connection.closes==0);
 assert(std::count(sent.begin(),sent.end(),QString("stop"))==0);
 w.Heartbeat();
 assert(w.pixelviewLease.heartbeatRequest==0);
 w.connection.message(R"({"type":"heartbeat","lease_expires_at":45000})");
 assert(w.pixelviewLease.ready && !w.pixelviewLease.authorized(0));
 staleSetup(true);assert(w.handler.starts==0);
 setup.set_value();
 Timer::run();
 assert(w.pixelviewLease.stopping && !w.button.enabled && !tray.enabled);
 assert(!w.pixelviewLease.requestStart(0));
 assert(!w.pixelviewStopPending && !w.pixelviewStreamingBusy);
 assert(output.forceStops==0 && w.service==&w.oldService);
 assert(w.pixelviewPairingDurable && w.pixelviewIdentity.nodeId=="fixture-node");
 assert(!w.pixelviewLease.authorized(0)); // Never retain failed ingest authority.
 const auto failureDetail=w.label.text;
 // If stop acknowledgement is pending, settle it before checking manual Start.
 if(w.pixelviewLease.stopping) w.connection.message(R"({"type":"stopped"})");
 w.RefreshPixelviewReconnect();
 assert(w.label.text==failureDetail);
 std::cerr << "after media failure: identity=" << w.identityLabel.text.toStdString()
           << ", ready=" << w.pixelviewLease.ready << ", socket closes=" << w.connection.closes
           << ", heartbeat=" << w.heartbeat.active << ", Start=" << w.button.enabled
           << ", tray Start=" << tray.enabled << ", reconnectAt=" << w.pixelviewReconnectAt
           << ", retryAt=" << w.pixelviewLease.retryAt << ", status=" << w.label.text.toStdString() << '\n';
 const bool connectedRetry = w.connection.closes==0 && w.pixelviewLease.ready && w.heartbeat.active &&
     w.identityLabel.text=="Node fixture-node · Connected" && w.button.enabled && tray.enabled;
 assert(connectedRetry && "media rejection must preserve authenticated control and usable manual Start");
 assert(std::count(sent.begin(),sent.end(),QString("stop"))==1);
 assert(w.pixelviewLease.requestStart(0));
 assert(w.pixelviewLease.pending && !w.pixelviewLease.authorized(0));
 assert(std::count(sent.begin(),sent.end(),QString("start"))==2);
 assert(!w.pixelviewLease.acceptSetup(failedGeneration,0));
 Timer::run();
 }
 // Control revocation/expiry must still fail closed during a media drain.
 for(bool revoked : {false,true}) {
  OBSBasic guard;guard.bind();guard.pixelviewLease.ready=true;guard.pixelviewLease.deadline=30000;
  assert(guard.pixelviewLease.requestStart(0));guard.pixelviewLease.pending=false;guard.pixelviewLease.leased=true;
  guard.pixelviewNativeAttempt=true;
  std::promise<void> pending;guard.setupStreamingGuard=pending.get_future().share();
  guard.MediaStopped(OBS_OUTPUT_INVALID_STREAM);
  if(revoked) guard.connection.disconnected(4401);
  else {guard.pixelviewClock.now=30000;guard.Watchdog();}
  assert(!guard.pixelviewLease.ready && !guard.pixelviewLease.intent && !guard.heartbeat.active);
  Timer::run();assert(guard.pixelviewStopPending && guard.connection.closes==0);
  pending.set_value();Timer::run();
  assert(!guard.pixelviewStopPending && guard.connection.closes==1 && !guard.button.enabled);
  if(revoked) assert(guard.unpairs==1 && guard.pixelviewReconnectAt==0);
 }
 // A synchronous stop ACK observes cleared authority, not a still-leased session.
 pixelview::Desktop ack;ack.ready=true;ack.deadline=30000;
 assert(ack.requestStart(0));ack.pending=false;ack.leased=true;
 ack.send=[&](QJsonObject o){assert(o["type"]=="stop");ack.receive({{"type","stopped"}},0);};
 ack.mediaStopped("Streaming failed");ack.outputStopped();
 assert(ack.ready && !ack.stopping && !ack.leased);
 // Healthy heartbeats cannot postpone missing stop acknowledgement forever.
 OBSBasic timeout;timeout.bind();timeout.pixelviewLease.ready=true;timeout.pixelviewLease.deadline=30000;
 assert(timeout.pixelviewLease.requestStart(0));
 timeout.pixelviewLease.pending=false;timeout.pixelviewLease.leased=true;
 timeout.pixelviewNativeAttempt=true;timeout.MediaStopped(OBS_OUTPUT_INVALID_STREAM);Timer::run();
 timeout.pixelviewClock.now=20000;timeout.Heartbeat();
 timeout.connection.message(R"({"type":"heartbeat"})");
 timeout.pixelviewClock.now=30000;timeout.Watchdog();Timer::run();
 assert(!timeout.pixelviewLease.ready && !timeout.pixelviewLease.intent && timeout.connection.closes==1);
 assert(timeout.pixelviewReconnectAt>0);
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
