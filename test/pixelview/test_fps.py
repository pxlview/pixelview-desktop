"""Compiled FPS behavior plus source contracts; GUI/hardware QA is separate."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FPS(unittest.TestCase):
    def compile_run(self, body):
        header = ROOT / 'frontend/utility/PixelviewFPS.hpp'
        self.assertTrue(header.exists(), 'FPS production policy is missing')
        with tempfile.TemporaryDirectory() as temp:
            source = pathlib.Path(temp) / 'test.cpp'
            binary = pathlib.Path(temp) / 'test'
            source.write_text('#include "frontend/utility/PixelviewFPS.hpp"\n#include "frontend/utility/PixelviewEncoding.hpp"\n#include <cassert>\n#include <vector>\nint main() {\n' + body + '\n}\n')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_exact_production_rates_and_existing_selection(self):
        self.compile_run('''
        using namespace pixelview;
        assert(FrameRates.size() == 8);
        const uint32_t nums[] = {24000,24,25,30000,30,50,60000,60};
        const uint32_t dens[] = {1001,1,1,1001,1,1,1001,1};
        const char *labels[] = {"23.976","24","25","29.97","30","50","59.94","60"};
        for (int i = 0; i < 8; ++i) {
            assert(FrameRates[i].num == nums[i] && FrameRates[i].den == dens[i]);
            assert(std::string(FrameRates[i].label) == labels[i]);
            assert(frameRateIndex(nums[i], dens[i]) == i);
            assert(frameRateIndex(nums[i]*2, dens[i]*2) == i);
        }
        assert(frameRateIndex(48,1) == -1); // Keep custom valid rates, don't force 30.
        assert(frameRateIndex(0,1) == -1);
        assert(frameRateIndex(30,0) == -1);
        ''')

    def test_transaction_reset_persist_rollback_and_active_guard(self):
        header = (ROOT / 'frontend/utility/PixelviewFPS.hpp').read_text()
        self.assertIn('changeFrameRate', header, 'Transactional FPS change is missing')
        self.compile_run('''
        using namespace pixelview;
        int config = 30, live = 30, disk = 30, resets = 0;
        bool active = true, resetOK = true, saveOK = true, rollbackOK = true;
        auto apply = [&] { config = 60; };
        auto restore = [&] { config = 30; };
        auto reset = [&] { ++resets; if (config == 60 ? resetOK : rollbackOK) { live = config; return true; } return false; };
        auto save = [&] { if (saveOK) disk = config; return saveOK; };
        auto change = [&] { return changeFrameRate(active, apply, restore, reset, save); };
        assert(change() == FPSChangeResult::Active);
        assert(config == 30 && resets == 0 && disk == 30);
        active = false; resetOK = false;
        assert(change() == FPSChangeResult::ResetFailed);
        assert(config == 30 && live == 30 && disk == 30 && resets == 2);
        resetOK = true; saveOK = false;
        assert(change() == FPSChangeResult::SaveFailed);
        assert(config == 30 && live == 30 && disk == 30);
        resetOK = false; rollbackOK = false;
        assert(change() == FPSChangeResult::RollbackFailed);
        assert(config == 30 && disk == 30);
        resetOK = true; rollbackOK = true; saveOK = true;
        assert(change() == FPSChangeResult::Success);
        assert(config == 60 && live == 60 && disk == 60);
        ''')

    def test_pending_native_setup_blocks_fps_before_any_side_effect(self):
        # Compile the actual two UI guard expressions, not a test-owned copy.
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        refresh = main.split('void OBSBasic::RefreshPixelviewFPS()', 1)[1].split('\nvoid ', 1)[0]
        select = main.split('void OBSBasic::SelectPixelviewFPS(int index)', 1)[1].split('\nvoid ', 1)[0]
        enabled_match = re.search(r'pixelviewFPS->setEnabled\(([^;]+)\);', refresh)
        busy_match = re.search(r'pixelview::changeFrameRate\(\s*(.*?)\s*,\s*\[&\]', select, re.S)
        assert enabled_match is not None, 'FPS enabled-state guard not found'
        assert busy_match is not None, 'FPS transaction guard not found'
        enabled, busy = enabled_match.group(1), busy_match.group(1)
        for guard in ('transaction', 'enabled'):
            with self.subTest(guard=guard):
                self.compile_run('''
        bool videoActive = false;
        auto obs_video_active = [&] { return videoActive; };
        struct Output { bool active = false; bool Active() const { return active; } } output;
        auto *outputHandler = &output;
        std::shared_future<void> setupStreamingGuard;
        auto enabled = [&] { return ENABLED; };
        auto busy = [&] { return BUSY; };
        int config = 30, live = 30, disk = 30;
        int applies = 0, restores = 0, resets = 0, saves = 0;
        auto change = [&] {
            return pixelview::changeFrameRate(busy(),
                [&] { ++applies; config = 60; },
                [&] { ++restores; config = 30; },
                [&] { ++resets; live = config; return true; },
                [&] { ++saves; disk = config; return true; });
        };
        assert(enabled() && !busy()); // Default-invalid native future is idle.
        std::promise<void> preparing;
        setupStreamingGuard = preparing.get_future().share();
        assert(setupStreamingGuard.valid());
        assert(setupStreamingGuard.wait_for(std::chrono::seconds{0}) == std::future_status::timeout);
        if (CHECK_ENABLED) {
            assert(!enabled());
        } else {
            assert(change() == pixelview::FPSChangeResult::Active);
            assert(config == 30 && live == 30 && disk == 30);
            assert(applies == 0 && restores == 0 && resets == 0 && saves == 0);
        }
        preparing.set_value(); // Ready but still valid must not remain busy.
        assert(setupStreamingGuard.valid());
        assert(enabled() && !busy());
        assert(change() == pixelview::FPSChangeResult::Success);
        assert(config == 60 && live == 60 && disk == 60);
        assert(applies == 1 && restores == 0 && resets == 1 && saves == 1);
        videoActive = true;
        assert(!enabled() && busy());
        assert(change() == pixelview::FPSChangeResult::Active);
        videoActive = false; output.active = true;
        assert(!enabled() && busy());
        assert(change() == pixelview::FPSChangeResult::Active);
        assert(applies == 1 && restores == 0 && resets == 1 && saves == 1);
        outputHandler = nullptr;
        assert(enabled() && !busy());
        '''.replace('CHECK_ENABLED', 'true' if guard == 'enabled' else 'false')
                    .replace('ENABLED', enabled).replace('BUSY', busy))

    def test_native_fps_guards_use_stream_setup_future(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        predicate = 'pixelview::encodingBusy(obs_video_active(), outputHandler && outputHandler->Active(), setupStreamingGuard)'
        refresh = main.split('void OBSBasic::RefreshPixelviewFPS()', 1)[1].split('\nvoid ', 1)[0]
        select = main.split('void OBSBasic::SelectPixelviewFPS(int index)', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('pixelviewFPS->setEnabled(!' + predicate + ');', refresh)
        self.assertIn('pixelview::changeFrameRate( ' + predicate + ',', ' '.join(select.split()))
        self.assertLess(select.index(predicate), select.index('config_set_uint'))
        self.assertLess(select.index(predicate), select.index('ResetVideo()'))
        native = (ROOT / 'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        self.assertIn('setupStreamingGuard = outputHandler->SetupStreaming(service, finish_stream_setup);', native)

    def test_native_ui_uses_transaction_and_preserves_startup_config(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('void OBSBasic::SelectPixelviewFPS(int index)', main,
                      'Native FPS control is missing')
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('\nvoid ', 1)[0]
        for text in ('QStringLiteral(" fps")', '"pixelviewFPS"', '"FPS"', '&OBSBasic::SelectPixelviewFPS',
                     'pixelview::FrameRates', 'RefreshPixelviewFPS()', 'config_save_safe', 'QMessageBox::warning'):
            self.assertIn(text, init)
        self.assertNotIn('config_set_uint(activeConfiguration, "Video", "FPS', init)
        select = main.split('void OBSBasic::SelectPixelviewFPS(int index)', 1)[1].split('\nvoid ', 1)[0]
        for text in ('pixelview::changeFrameRate', 'obs_video_active()', 'outputHandler->Active()',
                     '"FPSType", 2', '"FPSNum", rate.num', '"FPSDen", rate.den',
                     'config_has_user_value', 'config_remove_value', 'ResetVideo() == OBS_VIDEO_SUCCESS',
                     'config_save_safe', 'CONFIG_SUCCESS', 'QMessageBox::warning', 'RollbackFailed'):
            self.assertIn(text, select)
        self.assertNotIn('FitPixelview', select)
        self.assertNotIn('obs_sceneitem_', select)
        refresh = main.split('void OBSBasic::RefreshPixelviewFPS()', 1)[1].split('\nvoid ', 1)[0]
        for text in ('GetConfigFPS', 'frameRateIndex', 'QSignalBlocker', 'setEnabled', 'obs_video_active()', 'outputHandler->Active()'):
            self.assertIn(text, refresh)
        common = main.split('void OBSBasic::GetFPSCommon', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('strcmp(val, "24") == 0', common)
        header = (ROOT / 'frontend/widgets/OBSBasic.hpp').read_text()
        for method in ('OnActivate', 'OnDeactivate'):
            self.assertIn('RefreshPixelviewFPS();', header.split('inline void ' + method, 1)[1].split('\n\tinline ', 1)[0])


if __name__ == '__main__':
    unittest.main()
