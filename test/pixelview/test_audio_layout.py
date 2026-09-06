"""Compile the actual audio row against the app's Qt; no window is shown.

Run: python3 -m unittest discover -s test/pixelview -p test_audio_layout.py -v
Uses PIXELVIEW_QT_PREFIX or the Qt6_DIR in build_macos/CMakeCache.txt.
"""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AudioLayout(unittest.TestCase):
    def test_native_listen_row_fits_456px(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        row = 'auto *listenRow = new QHBoxLayout;' + audio.split(
            'auto *listenRow = new QHBoxLayout;', 1)[1].split('rows->addLayout(listenRow);', 1)[0]
        basic = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        sidebar_style = basic.split('sidebar->setStyleSheet(QStringLiteral(', 1)[1].split('));', 1)[0]
        prefix = os.environ.get('PIXELVIEW_QT_PREFIX')
        if not prefix:
            cache = (ROOT / 'build_macos/CMakeCache.txt').read_text()
            match = re.search(r'^Qt6_DIR:PATH=(.+)$', cache, re.M)
            self.assertIsNotNone(match, 'Configure the app once or set PIXELVIEW_QT_PREFIX')
            assert match is not None
            prefix = str(pathlib.Path(match[1]).parents[2])
        prefix = pathlib.Path(prefix)
        source = r'''
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QStyleFactory>
#include <QStyle>
#include <iostream>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    auto *native = QStyleFactory::create("macos");
    if (!native) { std::cerr << "Missing native macos style\n"; return 2; }
    app.setStyle(native);
    QFont font(QStringLiteral("Helvetica Neue"));
    font.setPixelSize(13); // Offscreen DPI must not inflate macOS point sizes.
    app.setFont(font);
    QWidget sidebar;
    sidebar.setStyleSheet(QStringLiteral(SIDEBAR_STYLE));
    auto *panel = new QWidget(&sidebar);
    panel->setFixedSize(456, 40);
    QCheckBox *pixelviewListen;
    auto *pixelviewStreamMute = new QCheckBox(QStringLiteral("Mute audio"), panel);
    QComboBox *pixelviewMonitorDevice;
    auto *rows = new QVBoxLayout(panel);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(4);
    AUDIO_ROW
    rows->addLayout(listenRow);
    QComboBox unrelated(&sidebar);
    unrelated.ensurePolished();
    const int unrelatedMinimum = unrelated.minimumWidth();
    bool ok = true;
    for (const auto &name : {QString("System default"), QString(180, 'W')}) {
        pixelviewMonitorDevice->clear();
        pixelviewMonitorDevice->addItem(name);
        for (bool checked : {false, true}) {
            pixelviewListen->setChecked(checked);
            panel->ensurePolished();
            pixelviewMonitorDevice->ensurePolished();
            // Also exercise a theme repolish, without creating native windows.
            pixelviewMonitorDevice->style()->unpolish(pixelviewMonitorDevice);
            pixelviewMonitorDevice->style()->polish(pixelviewMonitorDevice);
            listenRow->invalidate();
            listenRow->setGeometry(panel->rect());
            const auto m = pixelviewStreamMute->geometry();
            const auto c = pixelviewListen->geometry();
            ok &= listenRow->indexOf(pixelviewStreamMute) >= 0;
            ok &= c.x() >= m.right() + listenRow->spacing();
            ok &= m.center().y() == c.center().y();
            const auto d = pixelviewMonitorDevice->geometry();
            const int gap = d.x() - c.x() - c.width();
            std::cout << "nameLength=" << name.size() << " checked=" << checked
                      << " checkbox=" << c.x() << "," << c.width()
                      << " combo=" << d.x() << "," << d.width()
                      << " comboMinHint=" << pixelviewMonitorDevice->minimumSizeHint().width()
                      << " comboHint=" << pixelviewMonitorDevice->sizeHint().width()
                      << " comboMin=" << pixelviewMonitorDevice->minimumWidth()
                      << " checkboxHint=" << pixelviewListen->sizeHint().width()
                      << " gap=" << gap << " spacing=" << listenRow->spacing() << '\n';
            ok &= c.width() >= pixelviewListen->sizeHint().width();
            ok &= gap >= listenRow->spacing() && gap > 0;
            ok &= d.right() < 456 && d.width() >= 100;
            ok &= unrelated.minimumWidth() == unrelatedMinimum;
        }
    }
    if (!ok) std::cerr << "FAIL: native 456px audio row overlaps or clips\n";
    return ok ? 0 : 1;
}
'''.replace('SIDEBAR_STYLE', sidebar_style).replace('AUDIO_ROW', row)
        with tempfile.TemporaryDirectory(prefix='pixelview-audio-layout-') as tmp:
            tmp = pathlib.Path(tmp)
            cpp = tmp / 'audio.cpp'
            exe = tmp / 'audio'
            cpp.write_text(source)
            frameworks = prefix / 'lib'
            command = ['clang++', '-std=c++17', str(cpp), '-o', str(exe),
                       '-F' + str(frameworks), '-Wl,-rpath,' + str(frameworks)]
            for module in ('Core', 'Gui', 'Widgets'):
                command += ['-I' + str(frameworks / f'Qt{module}.framework/Headers'),
                            '-framework', 'Qt' + module]
            subprocess.run(command, check=True, capture_output=True, text=True)
            env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
                       QT_PLUGIN_PATH=str(prefix / 'plugins'))
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20)
            print(result.stdout, end='')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Mutation control: the old native layout rectangles must reproduce
            # the overlap; no fabricated min-width rule is needed.
            baseline = source.replace('listenRow->setSpacing(8);', '')
            baseline = re.sub(r'pixelview(?:Listen|MonitorDevice)->setAttribute\(Qt::WA_LayoutUsesWidgetRect\);', '', baseline)
            self.assertNotEqual(baseline, source)
            cpp.write_text(baseline)
            subprocess.run(command, check=True, capture_output=True, text=True)
            control = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20)
            print('Pre-fix control:\n' + control.stdout, end='')
            self.assertEqual(control.returncode, 1, control.stdout + control.stderr)
            self.assertRegex(control.stdout, r'gap=-\d+', 'Control must reproduce actual overlap')


if __name__ == '__main__':
    unittest.main()
