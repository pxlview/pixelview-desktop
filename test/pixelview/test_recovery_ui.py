"""Real Qt startup-dialog behavior and native sidebar separator contracts."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class RecoveryUI(unittest.TestCase):
    def test_unclean_dialog_continues_normal_mode_without_upload(self):
        source = (ROOT / 'frontend/OBSApp.cpp').read_text()
        function = source[source.index('typedef struct UncleanLaunchAction'):source.index('QAccessibleInterface *alignmentSelectorFactory')]
        self.assertIn('setWindowTitle(QStringLiteral("Pixelview Desktop"))', function)
        harness = r'''
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QCheckBox>
#include <QtCore/QTimer>
#include <cassert>
#define LOG_WARNING 1
#define LOG_INFO 2
#define blog(...) ((void)0)
#define QTStr(x) QStringLiteral(x)
''' + function + r'''
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    for (bool hasCrashLog : {false, true}) {
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            assert(box);
            // macOS QMessageBox intentionally ignores window titles.
            assert(box->text().contains(QStringLiteral("Pixelview Desktop did not shut down properly")));
            assert(box->buttons().size() == 1);
            auto *button = qobject_cast<QPushButton *>(box->buttons().front());
            assert(button && button->text() == QStringLiteral("Continue"));
            assert(box->defaultButton() == button);
            assert(!box->checkBox());
            button->click();
        });
        const auto action = handleUncleanShutdown(hasCrashLog);
        assert(!action.useSafeMode && !action.sendCrashReport);
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'dialog.cpp'
            cpp.write_text(harness)
            qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++', '-std=c++17', '-fPIC', '-F'+str(qt),
                            '-framework', 'QtCore', '-framework', 'QtGui', '-framework', 'QtWidgets',
                            '-Wl,-rpath,'+str(qt), str(cpp), '-o', tmp+'/dialog'], check=True)
            subprocess.run([tmp+'/dialog'], env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                           check=True, timeout=15)
        # Crash evidence detection and cleanup remain the upstream mechanism.
        self.assertIn('crashHandler_->hasUncleanShutdown()', source)
        self.assertIn('handleUncleanShutdown(hasNewCrashLog)', source)

    def test_pairing_has_same_native_separator_as_capture(self):
        source = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        block = source.split('InitPixelviewDesktop(sidebar);', 1)[1].split('pixelviewDevices =', 1)[0]
        self.assertIn('new QFrame(sidebar)', block)
        self.assertIn('setFrameShape(QFrame::HLine)', block)
        self.assertIn('sidebarLayout->addWidget(', block)
        capture = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        self.assertIn('divider->setFrameShape(QFrame::HLine);', capture)


if __name__ == '__main__':
    unittest.main()
