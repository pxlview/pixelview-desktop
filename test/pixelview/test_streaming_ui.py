"""Source integration contracts only: no GUI, encoder or network execution."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / 'frontend' / path).read_text()


class NativeStreamingUI(unittest.TestCase):
    def test_visible_button_is_the_native_lifecycle_control(self):
        main = source('widgets/OBSBasic.cpp')
        self.assertIn('void OBSBasic::InitPixelviewStreaming(QWidget *sidebar)', main,
                      'Expose the real native stream button, not a fake toggle')
        init = main.split('void OBSBasic::InitPixelviewStreaming(QWidget *sidebar)', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('controlsDock->findChild<QPushButton *>(QStringLiteral("streamButton"))', init)
        self.assertIn('sidebar->layout()->addWidget(streamButton)', init)
        self.assertIn('streamButton->show()', init)
        self.assertIn('InitPixelviewStreaming(sidebar);', main)
        self.assertNotIn('StartStreaming();', init, 'Initialization must never start output')
        self.assertNotIn('disconnect(', init, 'Keep native confirmations and error path connected')
        controls = source('widgets/OBSBasicControls.cpp')
        for signal in ('StreamingPreparing', 'StreamingStarting', 'StreamingStarted',
                       'StreamingStopping', 'StreamingStopped'):
            self.assertIn('&OBSBasic::' + signal, controls)
        for native in ('StreamActionTriggered', 'StartStreaming', 'StopStreaming', 'ForceStopStreaming'):
            self.assertIn('&OBSBasic::' + native, main)
        streaming = source('widgets/OBSBasic_Streaming.cpp')
        self.assertIn('UIValidation::StreamSettingsConfirmation(this, service)', streaming)
        self.assertIn('DisplayStreamStartError();', streaming)
        self.assertIn('OBSMessageBox::information(this, QTStr("Output.ConnectFail.Title")', streaming)
        self.assertIn('if (disableOutputsRef)', streaming)

    def test_compact_footer_preserves_native_telemetry_and_stats_popup(self):
        main = source('widgets/OBSBasic.cpp')
        self.assertTrue('ui->statusbar->show();' in main, 'Native CPU/stream telemetry is still hidden')
        self.assertTrue('ui->statusbar->addPermanentWidget(showStats);' in main)
        self.assertTrue('QStringLiteral("Show stats")' in main)
        self.assertTrue('&OBSBasic::on_stats_triggered' in main)
        self.assertTrue('QStringLiteral("recordFrame")' in main, 'Compact streaming strip should hide recording chrome')
        native = source('widgets/OBSBasicStatusBar.cpp')
        for metric in ('UpdateCPUUsage', 'UpdateBandwidth', 'UpdateDroppedFrames',
                       '"reconnect"', '"reconnect_success"', 'UpdateDelayMsg'):
            self.assertIn(metric, native)
        stats = source('widgets/OBSBasic_MainControls.cpp')
        self.assertIn('new OBSBasicStats(nullptr)', stats)
        detail = source('widgets/OBSBasicStats.cpp')
        for metric in ('obs_get_lagged_frames', 'video_output_get_skipped_frames',
                       'obs_output_get_frames_dropped'):
            self.assertIn(metric, detail)
        self.assertNotIn('packet loss', main.lower())

    def test_macos_avoids_duplicate_application_menu(self):
        main = source('widgets/OBSBasic.cpp')
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('#ifdef __APPLE__\n\tauto *appMenu = menuBar()->addMenu(QStringLiteral("Help"));', init)

    def test_license_menu_opens_offline_native_dialog(self):
        main = source('widgets/OBSBasic.cpp')
        self.assertIn('QStringLiteral("License information…")', main)
        self.assertIn('license->setMenuRole(QAction::NoRole)', main)
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('connect(license, &QAction::triggered, this, &OBSBasic::ShowPixelviewLicense)', init)
        self.assertNotIn('on_actionShowAbout_triggered', init)
        self.assertIn('void ShowPixelviewLicense();', source('widgets/OBSBasic.hpp'))
        dialog = main.split('void OBSBasic::ShowPixelviewLicense()', 1)[1].split('\nvoid ', 1)[0]
        for forbidden in ('OBSAbout', 'ShowAbout', 'invokeMethod', 'RemoteTextThread',
                          'QNetwork', 'https://', 'http://', 'setHtml'):
            self.assertNotIn(forbidden, dialog)
        for required in ('new QDialog(this)', 'Qt::WA_DeleteOnClose', 'new QPlainTextEdit',
                         'setReadOnly(true)', 'setPlainText(', 'QDialogButtonBox::Close',
                         '&QDialog::reject', 'dialog->show()',
                         'GetDataFilePath("license/COPYING", path)',
                         'licenseFile.open(QIODevice::ReadOnly)', 'licenseFile.readAll()',
                         'could not be loaded', 'Pixelview', 'OBS Studio', 'OBS Project contributors',
                         'GPL-2.0-or-later', 'WITHOUT ANY WARRANTY',
                         'not an official OBS Project release',
                         'Corresponding source must be made available under the GPL'):
            self.assertIn(required, dialog)


if __name__ == '__main__':
    unittest.main()
