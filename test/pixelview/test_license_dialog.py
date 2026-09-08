"""Compile the actual offline dialog body with Qt, without OBS/network/hardware."""
import os
from pathlib import Path
import subprocess
import shutil
import hashlib
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LicenseDialog(unittest.TestCase):
    def test_offline_tabs_and_full_licenses(self):
        source = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        production = source.split('void OBSBasic::ShowPixelviewLicense()', 1)[1].split('\nvoid OBSBasic::InitPixelview()', 1)[0]
        code = r'''
#include <QtWidgets/QtWidgets>
#include <QtCore/QtCore>
#include <cassert>
#include <string>
static QString dataRoot;
static bool GetDataFilePath(const char *name, std::string &out) {
    out = (dataRoot + "/" + name).toStdString();
    return QFile::exists(QString::fromStdString(out));
}
#define PIXELVIEW_VERSION "0.0.1"
#define PIXELVIEW_OBS_BASE_DESCRIBE "32.2.1-66-g6b3e55072"
class OBSBasic : public QWidget { public: void ShowPixelviewLicense(); };
void OBSBasic::ShowPixelviewLicense() PRODUCTION
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    dataRoot = argv[1];
    OBSBasic window;
    window.ShowPixelviewLicense();
    app.processEvents();
    auto *dialog = window.findChild<QDialog *>(); assert(dialog && dialog->isVisible());
    auto *tabs = dialog->findChild<QTabWidget *>();
    assert(tabs && tabs->count() == 3);
    assert(tabs->tabText(0) == "License");
    assert(tabs->tabText(1) == "Third-party notices");
    assert(tabs->tabText(2) == "Source & build info");
    for (int i=0; i<3; ++i) {
        tabs->setCurrentIndex(i); app.processEvents();
        auto *text = tabs->widget(i)->findChild<QPlainTextEdit *>();
        if (!text) text = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        assert(text && text->isReadOnly() && text->isVisible());
        assert(text->width() > 300 && text->height() > 100);
        const QImage rendered = text->viewport()->grab().toImage();
        int foregroundPixels = 0;
        const QColor background = text->palette().color(QPalette::Base);
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
                if (rendered.pixelColor(x, y) != background) ++foregroundPixels;
        assert(foregroundPixels > 100); // Real glyphs, not merely nonempty widget text.
        const QString contents = text->toPlainText();
        if (i == 0) {
            for (const char *name : {"COPYING", "gplv3.txt", "AUTHORS"}) {
                QFile file(dataRoot + "/license/" + name); assert(file.open(QIODevice::ReadOnly));
                assert(contents.contains(QString::fromUtf8(file.readAll())));
            }
            assert(contents.contains("GPL-3.0-or-later"));
            assert(contents.contains("FFmpeg"));
            assert(contents.contains("does not relicense"));
        } else {
            const QString name = i == 1 ? "third-party-notices.txt" : "source-manifest.json";
            QFile file(dataRoot + "/license/" + name);
            const bool opened = file.open(QIODevice::ReadOnly);
            const QString original = opened ? QString::fromUtf8(file.readAll()) : QString();
            if (!original.trimmed().isEmpty()) {
                assert(contents.contains(original));
                assert(contents.contains("fixture-end"));
                text->moveCursor(QTextCursor::End);
                text->verticalScrollBar()->setValue(text->verticalScrollBar()->maximum());
                app.processEvents();
                assert(text->verticalScrollBar()->value() == text->verticalScrollBar()->maximum());
                assert(text->cursorRect().intersects(text->viewport()->rect()));
                assert(!text->grab().isNull());
            } else {
                assert(contents.contains("Unavailable: bundled license/" + name + " could not be loaded."));
            }
        }
    }
    return 0;
}
'''.replace('PRODUCTION', production)
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'license.cpp'
            src.write_text(code)
            exe = Path(td) / 'license'
            subprocess.run(['clang++', '-std=c++17', '-fPIC', '-F' + str(qt),
                            '-I' + str(qt / 'QtWidgets.framework/Headers'),
                            '-I' + str(qt / 'QtGui.framework/Headers'),
                            '-I' + str(qt / 'QtCore.framework/Headers'),
                            '-framework', 'QtWidgets', '-framework', 'QtGui', '-framework', 'QtCore',
                            '-Wl,-rpath,' + str(qt), str(src), '-o', str(exe)], check=True)
            assets = Path(td) / 'data' / 'license'
            assets.mkdir(parents=True)
            for name in ('COPYING', 'gplv3.txt', 'AUTHORS'):
                shutil.copyfile(ROOT / 'frontend/data/license' / name, assets / name)
            for generated in ('absent', 'present', 'empty'):
                if generated == 'empty':
                    (assets / 'third-party-notices.txt').write_text('  \n')
                    (assets / 'source-manifest.json').write_text('')
                if generated == 'present':
                    (assets / 'third-party-notices.txt').write_text(
                        'Test-only partial SDK inventory, not a release notice\n' +
                        '\n'.join(f'fixture component {i}' for i in range(1000)) + '\nfixture-end')
                    (assets / 'source-manifest.json').write_text(json.dumps({
                        'schema_version': 1, 'version': '0.0.1', 'source_commit': 'a' * 40,
                        'source_url': 'https://example.invalid/immutable/0.0.1-1/source.tar.xz',
                        'source_status': 'unpublished-test-fixture',
                        'inventory': [f'fixture {i}' for i in range(1000)], 'end': 'fixture-end'
                    }, indent=2))
                subprocess.run([str(exe), str(assets.parent)], check=True,
                               env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'}, timeout=20)

    def test_official_license_assets_are_unchanged(self):
        self.assertEqual((ROOT / 'AUTHORS').read_bytes(), (ROOT / 'frontend/data/license/AUTHORS').read_bytes())
        self.assertEqual(hashlib.sha256((ROOT / 'frontend/data/license/gplv3.txt').read_bytes()).hexdigest(),
                         '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986')


if __name__ == '__main__':
    unittest.main()
