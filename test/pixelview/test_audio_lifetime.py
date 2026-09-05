"""Compile production ownership/cleanup against real Qt deferred deletion.

The small QObject meter uses the native onSourceDestroyed slot verbatim; this
isolates lifetime scheduling from libobs/audio hardware, not Qt object lifetime.
No app build, display server, or user configuration is involved.
"""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def body(text, signature):
    start = text.index('{', text.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start + 1:end - 1]


class AudioLifetime(unittest.TestCase):
    def test_autonomous_deletion_before_owner_refresh(self):
        header = (ROOT / 'frontend/widgets/OBSBasic.hpp').read_text()
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        native = (ROOT / 'frontend/components/VolumeMeter.hpp').read_text()
        declaration = re.search(r'^\s*[^\n;]+\bpixelviewMeter\s*(?:=[^;]*)?;', header, re.M).group().strip()
        cleanup = body(audio, 'void OBSBasic::ClearPixelviewAudio()')
        slot = body(native, 'void onSourceDestroyed()')
        refresh = body(audio, 'void OBSBasic::RefreshPixelviewAudio()')
        expired_condition = re.search(r'if \((previous != source[^\n]+)\) \{', refresh).group(1)
        qt_roots = sorted((ROOT / '.deps').glob('obs-deps-qt6-*/lib'))
        if os.environ.get('PIXELVIEW_QT_LIB'):
            qt_roots = [pathlib.Path(os.environ['PIXELVIEW_QT_LIB'])]
        self.assertTrue(qt_roots, 'Qt dependency required (or set PIXELVIEW_QT_LIB)')
        qt = qt_roots[-1]
        program = r'''
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QObject>
#include <cstdio>
#include <vector>
static int destroyed = 0;
class VolumeMeter : public QObject {
public:
    using QObject::QObject;
    ~VolumeMeter() override { ++::destroyed; }
    void onSourceDestroyed() { SLOT_BODY }
};
struct Owner {
    OWNER_DECLARATION
    std::vector<int> pixelviewAudioSignals;
    void *pixelviewAudioSource = nullptr;
    void ClearPixelviewAudio() { CLEANUP_BODY }
    void expiredSourceRefresh() {
        void *previous = nullptr, *source = nullptr;
        if (EXPIRED_CONDITION) ClearPixelviewAudio();
    }
};
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s\n", #condition); return 1; } } while (false)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Owner owner;
    owner.pixelviewMeter = new VolumeMeter;
    auto *native = static_cast<VolumeMeter *>(owner.pixelviewMeter);
    QMetaObject::invokeMethod(native, [native] { native->onSourceDestroyed(); }, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    CHECK(destroyed == 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(destroyed == 1);
    // Stop deterministically before dereferencing/double-deleting the old raw pointer.
    CHECK(owner.pixelviewMeter == nullptr);
    QMetaObject::invokeMethod(&app, [&] { owner.expiredSourceRefresh(); }, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    owner.ClearPixelviewAudio();
    CHECK(destroyed == 1);
    // Explicit owner clear while the native deleteLater is still queued.
    owner.pixelviewMeter = new VolumeMeter;
    owner.pixelviewMeter->onSourceDestroyed();
    owner.ClearPixelviewAudio();
    CHECK(owner.pixelviewMeter == nullptr);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(destroyed == 2);
    // QObject parent teardown also invalidates the owner's observer.
    auto *parent = new QObject;
    owner.pixelviewMeter = new VolumeMeter(parent);
    delete parent;
    CHECK(owner.pixelviewMeter == nullptr);
    owner.expiredSourceRefresh();
    owner.ClearPixelviewAudio();
    CHECK(destroyed == 3);
    std::puts("PASS: autonomous deletion before refresh; clear with deletion queued; parent teardown");
}
'''
        for key, value in {'SLOT_BODY': slot, 'OWNER_DECLARATION': declaration,
                           'CLEANUP_BODY': cleanup, 'EXPIRED_CONDITION': expired_condition}.items():
            program = program.replace(key, value)
        with tempfile.TemporaryDirectory(prefix='pixelview-audio-lifetime-') as directory:
            source = pathlib.Path(directory) / 'lifetime.cpp'
            executable = pathlib.Path(directory) / 'lifetime'
            source.write_text(program)
            compile_result = subprocess.run([
                'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-F', str(qt), '-I', str(qt / 'QtCore.framework/Headers'),
                '-framework', 'QtCore', '-Wl,-rpath,' + str(qt),
                str(source), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_close_clears_audio_before_scene_data_pumps_deletes(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        # applicationShutdown happens later, so its existing clear is too late.
        shutdown_scene = main[:main.index('\n\tClearSceneData();')]
        close_tail = shutdown_scene[shutdown_scene.rindex('\n\tdisableSaving++;'):]
        self.assertIn('pixelviewAudioShuttingDown = true;', close_tail)
        self.assertIn('ClearPixelviewAudio();', close_tail)
        self.assertLess(close_tail.index('pixelviewAudioShuttingDown = true;'),
                        close_tail.index('ClearPixelviewAudio();'))


if __name__ == '__main__':
    unittest.main()
