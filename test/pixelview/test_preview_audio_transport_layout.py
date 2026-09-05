"""Offscreen Qt geometry from production layout statements; not app/AX QA."""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PreviewAudioTransport(unittest.TestCase):
    def test_actual_audio_panel_below_canvas_and_transport_at_bottom(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        audio_layout = audio.split('void OBSBasic::InitPixelviewAudio(QWidget *previewContainer)', 1)[1].split('{', 1)[1].split('\n\tconnect(', 1)[0]
        sidebar_layout = main.split('auto *sidebarLayout = new QVBoxLayout(sidebar);', 1)[1].split('sidebar->setStyleSheet', 1)[0]
        sidebar_style = main.split('sidebar->setStyleSheet(QStringLiteral(', 1)[1].split('));', 1)[0]
        transport = 'auto *separator = new QFrame(sidebar);' + main.split('auto *separator = new QFrame(sidebar);', 1)[1].split('// Keep OBSBasicStatusBar', 1)[0]
        prefix = os.environ.get('PIXELVIEW_QT_PREFIX')
        if not prefix:
            cache = (ROOT / 'build_macos/CMakeCache.txt').read_text()
            match = re.search(r'^Qt6_DIR:PATH=(.+)$', cache, re.M)
            self.assertIsNotNone(match)
            assert match is not None
            prefix = str(pathlib.Path(match[1]).parents[2])
        prefix = pathlib.Path(prefix)
        code = r'''
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyleFactory>
#include <iostream>
#include <cassert>
int main(int argc, char **argv) {
 QApplication app(argc, argv);
 app.setStyle(QStyleFactory::create("macos"));
 QFont font("Helvetica Neue"); font.setPixelSize(13); app.setFont(font);
 QWidget root;
 auto *previewContainer = new QWidget(&root);
 auto *previewLayout = new QVBoxLayout(previewContainer);
 previewLayout->setContentsMargins(0, 0, 0, 0); previewLayout->setSpacing(0);
 auto *canvas = new QWidget(previewContainer);
 canvas->setMinimumSize(100, 100);
 previewLayout->addWidget(canvas, 1);
 QHBoxLayout *pixelviewMeterRow;
 QWidget *pixelviewMeterHost;
 QLabel *pixelviewMeterMuted;
 QCheckBox *pixelviewStreamMute, *pixelviewListen;
 QComboBox *pixelviewMonitorDevice;
 AUDIO
 pixelviewMonitorDevice->addItem(QString(180, 'W'));
 // Stub only the meter's size, not its layout or routing implementation.
 pixelviewMeterHost->setMinimumHeight(20);
 auto *sidebar = new QWidget(&root);
 auto *sidebarLayout = new QVBoxLayout(sidebar);
 SIDEBAR
 sidebar->setStyleSheet(QStringLiteral(SIDEBAR_STYLE));
 auto *settings = new QPushButton("Settings", sidebar);
 settings->setFixedHeight(36); sidebarLayout->addWidget(settings);
 sidebarLayout->addStretch(1);
 auto *controlsDock = new QWidget(&root);
 auto *nativeButton = new QPushButton("Start Streaming", controlsDock);
 nativeButton->setObjectName("streamButton"); nativeButton->setFixedHeight(36);
 TRANSPORT
 assert(nativeButton->parentWidget() == sidebar);
 for (const QSize size : {QSize(520, 440), QSize(1000, 800)}) {
  previewContainer->resize(size); sidebar->resize(340, size.height());
  previewContainer->ensurePolished(); sidebar->ensurePolished();
  previewLayout->setGeometry(previewContainer->rect());
  rows->setGeometry(panel->rect());
  pixelviewMeterRow->setGeometry(rows->itemAt(0)->geometry());
  listenRow->setGeometry(rows->itemAt(1)->geometry());
  sidebarLayout->setGeometry(sidebar->rect());
  const auto p = panel->geometry(), c = canvas->geometry(), b = nativeButton->geometry();
  assert(p.x() == 0 && p.top() == c.bottom() + 1);
  assert(p.bottom() == previewContainer->height() - 1);
  assert(p.width() <= 420 && p.width() >= 360);
  assert(pixelviewMonitorDevice->x() >= pixelviewListen->x() + pixelviewListen->width() + 8);
  assert(pixelviewMonitorDevice->geometry().right() < panel->width());
  // Native macOS widget rects can extend beyond their layout-item margins.
  assert(sidebarLayout->itemAt(sidebarLayout->count() - 1)->geometry().bottom() == sidebar->height() - sidebarLayout->contentsMargins().bottom() - 1);
  assert(b.bottom() < sidebar->height());
  assert(separator->geometry().bottom() < b.top());
  assert(settings->geometry().bottom() < separator->geometry().top());
  assert(nativeButton->isEnabled() && pixelviewListen->isEnabled() && pixelviewStreamMute->isEnabled());
  const auto meterHostGeometry = pixelviewMeterHost->geometry();
  pixelviewMeterMuted->show();
  previewLayout->setGeometry(previewContainer->rect());
  rows->setGeometry(panel->rect());
  pixelviewMeterRow->setGeometry(rows->itemAt(0)->geometry());
  assert(pixelviewMeterHost->geometry() == meterHostGeometry);
  assert(panel->geometry() == p);
  pixelviewMeterMuted->hide();
  std::cout << "preview=" << size.width() << 'x' << size.height()
            << " audio=" << p.x() << ',' << p.y() << ',' << p.width() << ',' << p.height()
            << " transportBottom=" << b.bottom() << '\n';
 }
}
'''.replace('AUDIO', audio_layout).replace('SIDEBAR_STYLE', sidebar_style).replace('SIDEBAR', sidebar_layout).replace('TRANSPORT', transport)
        with tempfile.TemporaryDirectory(prefix='pixelview-preview-audio-') as tmp:
            src, exe = pathlib.Path(tmp) / 'layout.cpp', pathlib.Path(tmp) / 'layout'
            src.write_text(code)
            frameworks = prefix / 'lib'
            command = ['clang++', '-std=c++17', str(src), '-o', str(exe), '-F' + str(frameworks), '-Wl,-rpath,' + str(frameworks)]
            for module in ('Core', 'Gui', 'Widgets'):
                command += ['-I' + str(frameworks / f'Qt{module}.framework/Headers'), '-framework', 'Qt' + module]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(exe)], env=dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_PLUGIN_PATH=str(prefix / 'plugins')), capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end='')


if __name__ == '__main__':
    unittest.main()
