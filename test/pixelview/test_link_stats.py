"""Desktop connection report (DESKTOP_STATS): compiled policy, RTCP parsing, a real
libdatachannel loopback and the frontend/plugin wiring. No app build, backend or engine."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'
QT = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'


def run(command, binary, expect):
    subprocess.run(command, check=True, capture_output=True, text=True)
    result = subprocess.run([binary], check=True, timeout=60, capture_output=True, text=True)
    assert expect in result.stdout, result.stdout


class LinkStats(unittest.TestCase):
    def test_rtcp_parsing(self):
        with tempfile.TemporaryDirectory() as tmp:
            run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                 '-fno-sanitize-recover=undefined', '-I'+str(ROOT),
                 str(ROOT/'test/pixelview/link_stats_native.cpp'), '-o', tmp+'/stats'],
                tmp+'/stats', 'link stats ok')

    def test_real_libdatachannel_loopback(self):
        with tempfile.TemporaryDirectory() as tmp:
            run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT),
                 '-I'+str(DEPS/'include'), '-L'+str(DEPS/'lib'), '-ldatachannel',
                 '-Wl,-rpath,'+str(DEPS/'lib'), str(ROOT/'test/pixelview/link_probe_loopback.cpp'),
                 '-o', tmp+'/loopback'],
                tmp+'/loopback', 'link probe loopback ok')

    def test_report_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC', '-I'+str(ROOT),
                 '-F'+str(QT/'lib'), '-framework', 'QtCore', '-Wl,-rpath,'+str(QT/'lib'),
                 str(ROOT/'test/pixelview/link_report.cpp'), '-o', tmp+'/report'],
                tmp+'/report', 'link report ok')

    def test_whole_computer_load(self):
        with tempfile.TemporaryDirectory() as tmp:
            run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT),
                 str(ROOT/'test/pixelview/system_load.cpp'), '-o', tmp+'/load'],
                tmp+'/load', 'system load ok')

    def test_whip_output_feeds_the_probe(self):
        output = (ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        # The probe runs the sender-report handler itself; adding both would send every report twice.
        self.assertIn('packetizer->addToChain(std::make_shared<pixelview::LinkProbe>(audio_sr_reporter, link));', output)
        self.assertIn('packetizer->addToChain(std::make_shared<pixelview::LinkProbe>(video_sr_reporter, link));', output)
        self.assertNotIn('addToChain(audio_sr_reporter)', output)
        self.assertNotIn('addToChain(video_sr_reporter)', output)
        self.assertIn('"void pixelview_link_stats(out bool reported, out int age_ms, out int rtt_ms, out float loss_pct, "',
                      output)
        # Cleared with the peer connection, under the lock the UI thread reads them with.
        stop = output.split('void WHIPOutput::StopThread(bool signal)', 1)[1].split('SendDelete();', 1)[0]
        self.assertIn('std::lock_guard<std::mutex> l(link_mutex);', stop)
        self.assertIn('video_link = nullptr;', stop)

    def test_frontend_reports_over_the_control_socket(self):
        inc = (ROOT/'frontend/widgets/OBSBasic_PixelviewStats.inc').read_text()
        self.assertIn('#include "OBSBasic_PixelviewStats.inc"', (ROOT/'frontend/widgets/OBSBasic.cpp').read_text())
        self.assertIn('proc_handler_call(obs_output_get_proc_handler(output), "pixelview_link_stats", &cd)', inc)
        self.assertIn('{"message", "DESKTOP_STATS"}, {"data", QJsonObject{{"stats", stats}}}', inc)
        # Sampled always, sent only on a ready socket, and only when it changed.
        push = inc.split('void OBSBasic::PushPixelviewStats()', 1)[1]
        self.assertLess(push.index('pixelviewLinkReport.sample('), push.index('if (!pixelviewLease.ready || pixelviewClosingSocket) return;'))
        self.assertLess(push.index('if (pixelviewLinkReport.take(stats))'), push.index('"DESKTOP_STATS"'))
        desktop = (ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        self.assertIn('InitPixelviewStats();', desktop)
        ready = desktop.split('if(!wasReady && pixelviewLease.ready) {', 1)[1].split('\n  }\n', 1)[0]
        self.assertIn('pixelviewLinkReport.forget();', ready)

    def test_control_round_trip_comes_from_the_answered_ping(self):
        watchdog = (ROOT/'frontend/utility/PixelviewSocketWatchdog.hpp').read_text()
        self.assertIn('const auto took=clock.elapsed()-pinged;', watchdog)
        self.assertIn('if(answered) answered((int)took);', watchdog)
        self.assertLess(watchdog.index('policy->pong();'), watchdog.index('if(answered) answered((int)took);'))
        mac = (ROOT/'frontend/utility/PixelviewDesktopMac.mm').read_text()
        self.assertIn('s->owner->controlRttMs=ms;', mac)
        close = mac.split('void DesktopConnection::closeSocket(bool normal) {', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('controlRttMs=-1;', close)
        # Windows and Linux: the Qt client times its own keepalive pings.
        qt = (ROOT/'frontend/utility/PixelviewWebSocket.cpp').read_text()
        self.assertIn('if (!pingPolicy || !pingPolicy->pending) return;', qt)
        self.assertIn('callback(int(clock.elapsed() - pingPolicy->sent));', qt)
        desktop_qt = (ROOT/'frontend/utility/PixelviewDesktopQt.cpp').read_text()
        self.assertIn('if (socket == client) controlRttMs = ms;', desktop_qt)
        self.assertIn('controlRttMs = -1;', desktop_qt.split('void DesktopConnection::closeSocket(bool normal)', 1)[1])


if __name__ == '__main__':
    unittest.main()
