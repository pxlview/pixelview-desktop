#pragma once

namespace pixelview {
enum class AudioChangeResult { Applied, ApplyFailed, SaveFailed, RollbackFailed };

// Native device acceptance does not prove audible playback.
template<typename Apply, typename Save, typename Restore>
AudioChangeResult changeAudio(Apply apply, Save save, Restore restore)
{
	if (!apply())
		return restore() ? AudioChangeResult::ApplyFailed : AudioChangeResult::RollbackFailed;
	if (!save())
		return restore() ? AudioChangeResult::SaveFailed : AudioChangeResult::RollbackFailed;
	return AudioChangeResult::Applied;
}
struct SourceAudioState {
	bool muted = false;
	bool monitoring = false;
};

inline SourceAudioState normalizeSourceAudio(SourceAudioState state)
{
	if (state.muted)
		state.monitoring = false;
	return state;
}

inline SourceAudioState sourceAudioTarget(SourceAudioState previous, bool monitoring, bool checked)
{
	// Unmuting never automatically resumes local playback.
	return monitoring ? normalizeSourceAudio({previous.muted, checked}) : SourceAudioState{checked, false};
}

template<typename Apply, typename Save>
AudioChangeResult changeSourceAudio(SourceAudioState previous, SourceAudioState target, Apply apply, Save save)
{
	apply(target);
	if (save())
		return AudioChangeResult::Applied;
	apply(previous);
	return save() ? AudioChangeResult::SaveFailed : AudioChangeResult::RollbackFailed;
}
} // namespace pixelview
