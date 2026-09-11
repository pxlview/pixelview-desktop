"""Compile stock Designer menus + actual minimal-menu/platform code offline.

No OBS startup, log reads, credential access, uploads or desktop URL opening.
Native slot bodies are audited separately; dispatch uses harmless counters.
"""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


class DiagnosticsMenu(unittest.TestCase):
    def test_actual_native_menu_and_platform_filter(self):
        source = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        menu = source.split('void OBSBasic::InitPixelview()', 1)[1]
        menu = menu[menu.index('\tfor (auto *action : menuBar()->actions())'):menu.index('\tSystemTray(false);')]
        gate = source[source.index('#if !defined(_WIN32)\n\tdelete ui->actionRepair;'):]
        gate = gate[:gate.index('\n#endif\n#endif') + len('\n#endif\n#endif')]
        # Only switch production platform directives, never Qt's platform headers.
        production = (gate + '\n' + menu).replace('__APPLE__', 'TEST_APPLE').replace('_WIN32', 'TEST_WINDOWS')
        original = ET.parse(ROOT / 'frontend/forms/OBSBasic.ui').getroot()
        form = ET.Element('ui', version='4.0')
        ET.SubElement(form, 'class').text = 'OBSBasic'
        widget = ET.SubElement(form, 'widget', {'class': 'QMainWindow', 'name': 'OBSBasic'})
        menubar = original.find("widget/widget[@class='QMenuBar']")
        assert menubar is not None
        widget.append(copy.deepcopy(menubar))
        for action in original.findall('widget/action'):
            widget.append(copy.deepcopy(action))
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        code = r'''
#include <QtWidgets/QtWidgets>
#include <cassert>
#include <iostream>
#include "menu.h"
class OBSBasic : public QMainWindow {
 Q_OBJECT
public:
 Ui::OBSBasic storage; Ui::OBSBasic *ui = &storage;
 int logs=0, view=0, crashes=0, licenses=0;
 void ShowPixelviewLicense() { ++licenses; }
 void InitMenu() {
PRODUCTION
 }
public slots:
 void on_actionShowLogs_triggered() { ++logs; }
 void on_actionViewCurrentLog_triggered() { ++view; }
 void on_actionShowCrashLogs_triggered() { ++crashes; }
};
#include "menu-test.moc"
static QStringList visible(QMenu *menu) {
 QStringList names;
 for (auto *a : menu->actions()) if (a->isVisible() && !a->isSeparator()) names << a->text();
 return names;
}
int main(int argc, char **argv) {
 QApplication app(argc, argv); OBSBasic w; w.ui->setupUi(&w);
 auto *logs = w.ui->menuLogFiles;
 auto *crashes = w.ui->menuCrashLogs;
 auto *oldHelp = w.ui->menuBasic_MainMenu_Help;
 w.InitMenu(); w.show(); app.processEvents();
 assert(w.menuBar()->actions().size() == 1);
 auto *help = w.menuBar()->actions().front()->menu();
#ifdef TEST_APPLE
 assert(help->title() == "Help");
#else
 assert(help->title() == "Pixelview Desktop");
#endif
 assert(!oldHelp->menuAction()->isVisible() && !oldHelp->menuAction()->isEnabled());
 assert(help->actions().contains(logs->menuAction()) && "Native Log Files submenu missing");
 assert(logs == w.ui->menuLogFiles && logs->isEnabled() && logs->menuAction()->isVisible());
 assert(visible(logs) == QStringList({"Basic.MainMenu.Help.Logs.ShowLogs", "Basic.MainMenu.Help.Logs.ViewCurrentLog"}));
 logs->popup(QPoint(20,20)); app.processEvents();
 assert(logs->isVisible() && !logs->actionGeometry(w.ui->actionShowLogs).isEmpty());
 logs->hide();
 w.ui->actionShowLogs->trigger(); w.ui->actionViewCurrentLog->trigger();
 assert(w.logs == 1 && w.view == 1);
 QStringList expected;
#if defined(TEST_APPLE) || defined(TEST_WINDOWS)
 assert(w.ui->menuCrashLogs == crashes && help->actions().contains(crashes->menuAction()));
 assert(crashes->isEnabled() && crashes->menuAction()->isVisible());
 assert(visible(crashes) == QStringList({"Basic.MainMenu.Help.CrashLogs.ShowLogs"}));
 w.ui->actionShowCrashLogs->trigger(); assert(w.crashes == 1);
#else
 assert(w.ui->menuCrashLogs == nullptr);
#endif
#ifdef ENABLE_SPARKLE_UPDATER
 expected << "Check for Updates…";
#endif
 expected << "Basic.MainMenu.Help.Logs";
#if defined(TEST_APPLE) || defined(TEST_WINDOWS)
 expected << "Basic.MainMenu.Help.CrashLogs";
#endif
 expected << "License information…" << "Quit Pixelview Desktop";
 assert(visible(help) == expected);
 for (auto *a : help->actions()) {
  if (a->text() == "License information…") { assert(a->menuRole() == QAction::NoRole); a->trigger(); }
  if (a->text() == "Quit Pixelview Desktop") assert(a->menuRole() == QAction::QuitRole);
 }
 assert(w.licenses == 1);
 std::cout << "native menu/platform/dispatch/license passed\n";
}
'''.replace('PRODUCTION', production)
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            ET.ElementTree(form).write(temp / 'menu.ui', encoding='unicode')
            subprocess.run([str(qt.parent / 'libexec/uic'), str(temp / 'menu.ui'), '-o', str(temp / 'menu.h')], check=True)
            (temp / 'menu-test.cpp').write_text(code)
            subprocess.run([str(qt.parent / 'libexec/moc'), str(temp / 'menu-test.cpp'), '-o', str(temp / 'menu-test.moc')], check=True)
            for platform, defines in [('mac', ['TEST_APPLE']), ('mac-sparkle', ['TEST_APPLE', 'ENABLE_SPARKLE_UPDATER']), ('linux-gate', []), ('windows-gate', ['TEST_WINDOWS'])]:
                with self.subTest(platform=platform):
                    exe = temp / platform
                    subprocess.run(['clang++', '-std=c++17', '-fPIC', '-F' + str(qt),
                                    *['-D' + define for define in defines],
                                    *['-I' + str(qt / (name + '.framework/Headers')) for name in ('QtWidgets', 'QtGui', 'QtCore')],
                                    '-framework', 'QtWidgets', '-framework', 'QtGui', '-framework', 'QtCore',
                                    '-Wl,-rpath,' + str(qt), str(temp / 'menu-test.cpp'), '-o', str(exe)], check=True)
                    result = subprocess.run([str(exe)], env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                                            capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_handlers_remain_local_and_uploads_unconfigured(self):
        controls = (ROOT / 'frontend/widgets/OBSBasic_MainControls.cpp').read_text()
        self.assertIn('GetAppConfigPath(logDir, sizeof(logDir), "obs-studio/logs")', controls)
        self.assertIn('logView = new OBSLogViewer();', controls)
        self.assertIn('App()->openCrashLogDirectory();', controls)
        self.assertIn('Pixelview log upload is not configured.', controls)
        mac = (ROOT / 'frontend/utility/CrashHandler_MacOS.mm').read_text()
        self.assertIn('URLByAppendingPathComponent:@"logs/DiagnosticReports"', mac)
        app = (ROOT / 'frontend/OBSApp.cpp').read_text()
        handler = app.split('void OBSApp::openCrashLogDirectory() const', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('crashHandler_->getCrashLogDirectory()', handler)
        self.assertIn('QDesktopServices::openUrl(crashLogDirectoryURL)', handler)


if __name__ == '__main__':
    unittest.main()
