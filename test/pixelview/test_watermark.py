"""Sender watermark: policy (native), WHIP declaration and frontend wiring."""
import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
class SenderWatermark(unittest.TestCase):
    def test_policy_native(self):
        qt = ROOT/'.deps/obs-deps-qt6-2026-08-26-universal'
        frameworks = ROOT/'build_macos/libobs/RelWithDebInfo'
        deps = ROOT/'.deps/obs-deps-2026-08-26-universal'
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT/'frontend'),'-I'+str(ROOT/'libobs'),'-I'+str(ROOT/'build_macos/config'),
                            '-I'+str(ROOT/'build_macos/libobs'),'-I'+str(deps/'include'),'-F'+str(qt/'lib'),'-framework','QtCore',
                            '-F'+str(frameworks),'-framework','libobs','-Wl,-rpath,'+str(frameworks),'-Wl,-rpath,'+str(qt/'lib'),
                            '-Wl,-rpath,'+str(deps/'lib'),str(ROOT/'test/pixelview/watermark_native.cpp'),'-o',tmp+'/test'],check=True)
            subprocess.run([tmp+'/test'],check=True)
    def test_whip_declares_and_reads_the_echo(self):
        whip = (ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        self.assertIn('watermark_declared = obs_data_get_bool(service_settings, "pixelview_watermark");', whip)
        self.assertIn('watermark_acknowledged = false;', whip[whip.index('bool WHIPOutput::Init()'):])
        self.assertIn('"X-Pixelview-Watermark: sender"', whip)
        self.assertIn('value_for_header("x-pixelview-watermark", http_header)', whip)
        self.assertIn('"void pixelview_watermark(out bool declared, out bool acknowledged)"', whip)
    def test_frontend_wiring(self):
        desktop = (ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        publish = desktop[desktop.index('pixelviewLease.publish='):desktop.index('StartStreaming();', desktop.index('pixelviewLease.publish='))]
        # Declared on the service before the output starts, after the colour profile is applied.
        self.assertIn('obs_data_set_bool(settings,"pixelview_watermark",DeclarePixelviewWatermark());', publish)
        self.assertLess(publish.index('ApplyPixelviewColor(true)'), publish.index('DeclarePixelviewWatermark()'))
        halt = desktop[desktop.index('pixelviewLease.halt='):desktop.index('pixelviewLease.publish=')]
        self.assertIn('PixelviewWatermarkStopped();', halt)
        stopped = desktop[desktop.index('void OBSBasic::PixelviewOutputStopped()'):desktop.index('void OBSBasic::QueuePixelviewOutputStopped')]
        self.assertIn('PixelviewWatermarkStopped();', stopped)
        self.assertIn('PixelviewWatermarkStarted();', desktop)
        main = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('#include "OBSBasic_PixelviewWatermark.inc"', main)
        shutdown = main[main.index('void OBSBasic::applicationShutdown()'):]
        self.assertIn('ShutdownPixelviewWatermark();', shutdown[:shutdown.index('obs_shutdown')] if 'obs_shutdown' in shutdown else shutdown)
        inc = (ROOT/'frontend/widgets/OBSBasic_PixelviewWatermark.inc').read_text()
        self.assertIn('obs_add_main_rendered_callback(pixelview::WatermarkOverlay::rendered', inc)
        self.assertIn('obs_remove_main_rendered_callback(pixelview::WatermarkOverlay::rendered', inc)
        # Private source: never in the saved scene or the Sources list.
        self.assertIn('obs_source_create_private("image_source"', inc)
        self.assertNotIn('obs_scene_add', inc)
        self.assertIn('obs_data_set_bool(settings, "linear_alpha", true);', inc)
        # Device token only to the paired origin, no redirects.
        self.assertIn('ManualRedirectPolicy', inc)
        self.assertIn('pixelview::Desktop::validOrigin(pixelviewOrigin, pixelviewDev)', inc)
if __name__ == '__main__':
    unittest.main()
