"""Source integration contracts; not a substitute for a built Qt/hardware smoke test."""
import pathlib
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]


class MinimalShell(unittest.TestCase):
    def test_native_device_workflow_and_one_shot_fit(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertTrue('void OBSBasic::RefreshPixelviewDevices()' in main, 'Native device workflow missing')
        refresh = main.split('void OBSBasic::RefreshPixelviewDevices()', 1)[1].split('\nvoid ', 1)[0]
        for contract in ('obs_get_source_properties("decklink-input")', 'obs_property_list_item_disabled',
                         '"device_hash"', 'pixelview::captureStatus', 'QSignalBlocker',
                         'Input signal is not verified'):
            self.assertTrue(contract in refresh, contract)
        self.assertNotIn('obs_source_update(', refresh)
        self.assertNotIn('FitPixelview', refresh)
        select = main.split('void OBSBasic::SelectPixelviewDevice(', 1)[1].split('\nvoid ', 1)[0]
        for contract in ('obs_source_create("decklink-input"', 'obs_properties_apply_settings',
                         'obs_property_modified', 'obs_source_update', 'pixelviewFitPolicy.sourceCreated()'):
            self.assertTrue(contract in select, contract)
        fit = main.split('void OBSBasic::FitPixelviewCapture(', 1)[1].split('\nvoid ', 1)[0]
        for contract in ('pixelviewFitPolicy.takeRequest()', 'OBS_BOUNDS_SCALE_INNER', 'OBS_ALIGN_CENTER',
                         'obs_sceneitem_set_crop', 'obs_sceneitem_set_info2', 'SaveProject()'):
            self.assertTrue(contract in fit, contract)
        self.assertIn('CreatePropertiesWindow(source)', main)
        self.assertIn('pixelviewRefreshTimer->stop()', main)

    def test_reselecting_current_device_keeps_native_settings(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        select = main.split('void OBSBasic::SelectPixelviewDevice(', 1)[1].split('\nvoid ', 1)[0]
        self.assertTrue('pixelview::shouldChangeDevice(' in select, 'Same-device selection must be a no-op')
        self.assertTrue('pixelview::chooseOption(' in select, 'Keep still-supported input choices on device switch')
        self.assertLess(select.index('pixelview::shouldChangeDevice('), select.index('obs_data_set_string(settings'))

    def test_available_device_help_is_truthful_without_footer(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertNotIn('Device selected • Local preview', main)
        self.assertIn('pixelviewDevices->setToolTip(captureHelp)', main)
        self.assertIn('Input signal is not verified independently', main)

    def test_redundant_status_toolbar_is_absent(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertNotIn('pixelviewStatusBar', main)
        self.assertNotIn('footer->addWidget', main)

    def test_launch_is_preview_only_and_keeps_native_preview(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('void OBSBasic::InitPixelview()', main, 'Minimal Pixelview shell missing')
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('\nvoid ', 1)[0]
        for contract in ('dock->hide()', 'menuBar()->clear()', 'ui->contextContainer->hide()',
                         'EnablePreviewDisplay(true)', 'SetPreviewProgramMode(false)',
                         '"Pixelview"', '"Fit"', '"Device settings…"',
                         'InitPixelview();'):
            self.assertIn(contract, main)
        self.assertIn('ui->preview->SetLocked(false)', init)
        self.assertNotIn('on_autoConfigure_triggered, Qt::QueuedConnection', main)
        self.assertIn('TimedCheckForUpdates();', main)
        self.assertNotIn('Auth::Load();', main)
        entry = (ROOT / 'frontend/obs-main.cpp').read_text()
        self.assertNotIn('check.exec();', entry)
        scenes = (ROOT / 'frontend/widgets/OBSBasic_SceneCollections.cpp').read_text()
        self.assertNotIn('CreateFirstRunSources();', scenes)
        self.assertIn('ovi.base_width = pixelview::CanvasWidth;', main)
        self.assertIn('ovi.output_width = pixelview::CanvasWidth;', main)
        self.assertIn('ovi.base_height = pixelview::CanvasHeight;', main)
        self.assertIn('ovi.output_height = pixelview::CanvasHeight;', main)


if __name__ == '__main__':
    unittest.main()
