"""Compile the production master policy without app/config/audio hardware."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class MasterAudio(unittest.TestCase):
    def test_compiled_master_states_rollback_and_legacy_normalization(self):
        with tempfile.TemporaryDirectory() as temp:
            src = pathlib.Path(temp) / 'master.cpp'
            exe = pathlib.Path(temp) / 'master'
            src.write_text(r'''#include "frontend/utility/PixelviewAudio.hpp"
#include <cassert>
#include <cstdio>
int main() {
 using namespace pixelview;
 for (bool mute : {false, true}) for (bool listen : {false, true}) {
  SourceAudioState old{mute, listen};
  auto muted = sourceAudioTarget(old, false, true);
  assert(muted.muted && !muted.monitoring);
  auto unmuted = sourceAudioTarget(old, false, false);
  assert(!unmuted.muted && !unmuted.monitoring);
  auto local = sourceAudioTarget(old, true, true);
  assert(local.muted == mute && local.monitoring == !mute);
  auto off = sourceAudioTarget(old, true, false);
  assert(off.muted == mute && !off.monitoring);
  auto live = old; auto disk = old; int saves = 0;
  auto result = changeSourceAudio(old, muted,
   [&](SourceAudioState s) { live = s; },
   [&] { ++saves; if (saves == 1) return false; disk = live; return true; });
  assert(result == AudioChangeResult::SaveFailed);
  assert(live.muted == mute && live.monitoring == listen);
  assert(disk.muted == mute && disk.monitoring == listen && saves == 2);
  result = changeSourceAudio(old, muted, [&](SourceAudioState s) { live = s; }, [] { return false; });
  assert(result == AudioChangeResult::RollbackFailed);
  assert(live.muted == mute && live.monitoring == listen);
  result = changeSourceAudio(old, muted, [&](SourceAudioState s) { live = s; }, [&] { disk = live; return true; });
  assert(result == AudioChangeResult::Applied && live.muted && !live.monitoring && !disk.monitoring);
  auto normalized = normalizeSourceAudio(old);
  assert(normalized.muted == mute && normalized.monitoring == (listen && !mute));
 }
 SourceAudioState defaults{};
 assert(!defaults.muted && !defaults.monitoring);
 std::puts("PASS: master combinations, local guard, both-field rollback, legacy normalization, defaults");
}'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>'))
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_compiled_startup_fail_closed_save_retry_once_warning(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        block = audio.split('// persistence on polling without re-setting native flags or recursive saves.', 1)[1].split('const bool muted =', 1)[0]
        program = r'''#include "frontend/utility/PixelviewAudio.hpp"
#include <cassert>
#include <cstdio>
struct Source { bool muted = true, monitoring = true; };
bool obs_source_muted(Source *s) { return s->muted; }
bool obs_source_get_monitoring_enabled(Source *s) { return s->monitoring; }
int writes = 0, warnings = 0;
void obs_source_set_monitoring_enabled(Source *s, bool b) { ++writes; s->monitoring = b; }
constexpr int LOG_WARNING = 1;
void blog(int, const char *) { ++warnings; }
struct Owner {
 bool pixelviewAudioNormalizationPending = false, pixelviewAudioNormalizationWarned = false;
 bool disableSaving = true, saveOK = false; int saves = 0;
 bool diskMuted = true, diskMonitoring = true;
 bool SavePixelviewAudioSource(Source *s) {
  ++saves; if (!saveOK) return false;
  diskMuted = s->muted; diskMonitoring = s->monitoring; return true;
 }
 void refresh(Source *source) { BLOCK }
};
int main() {
 Source legacy; Owner owner;
 owner.refresh(&legacy);
 assert(legacy.muted && !legacy.monitoring && writes == 1 && owner.saves == 0);
 assert(owner.pixelviewAudioNormalizationPending);
 owner.disableSaving = false;
 owner.refresh(&legacy); owner.refresh(&legacy);
 assert(legacy.muted && !legacy.monitoring && writes == 1 && warnings == 1 && owner.saves == 2);
 owner.saveOK = true; owner.refresh(&legacy);
 assert(owner.diskMuted && !owner.diskMonitoring && !owner.pixelviewAudioNormalizationPending);
 owner.refresh(&legacy); assert(owner.saves == 3);
 legacy.monitoring = true; owner.refresh(&legacy);
 assert(!legacy.monitoring && writes == 2 && owner.saves == 4);
 owner.refresh(nullptr); assert(owner.saves == 4);
 std::puts("PASS: production startup normalization, deferred save, fail-closed retry, warning once, external flags");
}'''.replace('BLOCK', block)
        with tempfile.TemporaryDirectory() as temp:
            src = pathlib.Path(temp) / 'normalize.cpp'
            exe = pathlib.Path(temp) / 'normalize'
            src.write_text(program)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_ui_master_guard_inactive_meter_and_normalization(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        self.assertIn('new QCheckBox(QStringLiteral("Mute audio")', audio)
        self.assertIn('setAccessibleName(QStringLiteral("Mute audio"))', audio)
        self.assertIn('new QLabel(QStringLiteral("Audio muted"), pixelviewMeterHost)', audio)
        self.assertIn('pixelviewMeter->setVisible(!muted)', audio)
        self.assertIn('pixelviewMeterMuted->setVisible(muted)', audio)
        self.assertIn('meterPolicy.setRetainSizeWhenHidden(true)', audio)
        self.assertIn('!muted && obs_audio_monitoring_available()', audio)
        self.assertIn('monitoring && checked && obs_source_muted(source)', audio)
        self.assertIn('pixelview::changeSourceAudio', audio)
        self.assertIn('pixelview::normalizeSourceAudio', audio)
        self.assertIn('QScopedValueRollback<bool>', audio)
        self.assertIn('pixelviewAudioNormalizationPending && !disableSaving', audio)
        self.assertNotIn('setLevels(', audio)
        self.assertEqual(audio.count('new VolumeMeter('), 1)
        change = audio.split('void OBSBasic::ChangePixelviewAudio(', 1)[1].split('\nvoid ', 1)[0]
        self.assertLess(change.index('RefreshPixelviewAudio();'), change.index('auto source ='))
        self.assertIn('QScopedValueRollback<bool> changing(pixelviewAudioRefreshing, true)', change)

if __name__ == '__main__':
    unittest.main()
