#pragma once
#include <future>
#include <initializer_list>
#include <string>
#include <vector>

namespace pixelview {
inline bool encodingBusy(bool videoActive, bool outputActive, const std::shared_future<void> &setup)
{
	return videoActive || outputActive ||
	       (setup.valid() && setup.wait_for(std::chrono::seconds{0}) != std::future_status::ready);
}
struct EncoderChoice {
	std::string id;
	std::string codec;
	bool hardware;
};
// OBS has no hardware capability bit. Only classify known public backend IDs;
// callers must still enumerate registered types and filter internal/deprecated ones.
// VT IDs match upstream SimpleOutput and actual VT IsHardwareAccelerated metadata.
// Unknown IDs fail closed; registration is not proof of session/device support.
inline bool isHardwareEncoder(const std::string &id)
{
	for (const char *known : {
		     "com.apple.videotoolbox.videoencoder.ave.avc",
		     "com.apple.videotoolbox.videoencoder.ave.hevc",
		     "com.apple.videotoolbox.videoencoder.h264.gva", // Upstream VT Intel hardware alias.
		     "obs_nvenc_h264_tex", "obs_nvenc_hevc_tex", "obs_nvenc_av1_tex",
		     "ffmpeg_nvenc", "ffmpeg_hevc_nvenc",
		     "obs_qsv11_v2", "obs_qsv11_hevc", "obs_qsv11_av1",
		     "h264_texture_amf", "h265_texture_amf", "av1_texture_amf",
		     "ffmpeg_vaapi_tex", "hevc_ffmpeg_vaapi_tex", "av1_ffmpeg_vaapi_tex"}) {
		if (id == known) return true;
	}
	return false;
}
inline int preferredEncoder(const std::vector<EncoderChoice> &encoders, bool apple)
{
	int best = -1, score = -1;
	for (size_t i = 0; i < encoders.size(); ++i) {
		const auto &e = encoders[i];
		int rank = e.id == "obs_x264" ? 0 : -1;
		if (e.hardware) rank = e.codec == "hevc" ? 20 : 10;
		if (apple && e.hardware && e.codec == "hevc" && e.id.find("com.apple.videotoolbox.") == 0) rank = 30;
		if (rank > score) { best = static_cast<int>(i); score = rank; }
	}
	return best;
}
// obs-x264 parses space-separated name=value options in order after preset/tune.
// Retain unrelated options, remove competing values, then force the final override.
inline std::string withoutBFrameOverrides(const std::string &options)
{
	std::string result;
	size_t start = 0;
	while (start < options.size()) {
		start = options.find_first_not_of(' ', start);
		if (start == std::string::npos) break;
		auto end = options.find(' ', start);
		const auto token = options.substr(start, end == std::string::npos ? end : end - start);
		if (token.compare(0, 8, "bframes=") != 0) {
			if (!result.empty()) result += ' ';
			result += token;
		}
		if (end == std::string::npos) break;
		start = end + 1;
	}
	if (!result.empty()) result += ' ';
	return result + "bframes=0";
}
// Native NVENC shares obs_parse_options: literal spaces delimit tokens, the
// first '=' separates a case-sensitive name and nonempty value. Remove only
// accepted frameIntervalP overrides; bf=0 then retains the native IP/all-I GOP.
// Preserve unrelated bytes, including malformed options the consumer ignores.
inline std::string withoutNvencBFrameOverrides(const std::string &options)
{
	std::string result;
	for (size_t start = 0; start < options.size();) {
		auto end = options.find(' ', start);
		if (end == std::string::npos) end = options.size();
		const auto token = options.substr(start, end - start);
		if (token.size() <= 15 || token.compare(0, 15, "frameIntervalP=") != 0)
			result += token;
		if (end < options.size()) result += ' ';
		start = end + 1;
	}
	return result;
}
// AMF's direct-property fallback accepts SDK names after bf-derived counts.
// GPUOpen AMF VideoEncoderVCE.h / VideoEncoderAV1.h define these amf_int64
// properties (verified at c35f613aea2e5057a688c979e75b1cf24253297e).
// Use the same literal-space/nonempty-value grammar as obs_parse_options;
// retain every unrelated byte, including ignored tokens and other codec keys.
inline std::string withoutAmfBFrameOverrides(const std::string &options, bool av1)
{
	const std::string count = av1 ? "Av1MaxConsecutiveBPictures" : "MaxConsecutiveBPictures";
	const std::string pattern = av1 ? "Av1BPicturesPattern" : "BPicturesPattern";
	std::string result;
	for (size_t start = 0; start < options.size();) {
		auto end = options.find(' ', start);
		if (end == std::string::npos) end = options.size();
		const auto token = options.substr(start, end - start);
		const auto assign = token.find('=');
		const bool override = assign != std::string::npos && assign + 1 < token.size() &&
			(token.substr(0, assign) == count || token.substr(0, assign) == pattern);
		if (!override) result += token;
		if (end < options.size()) result += ' ';
		start = end + 1;
	}
	return result;
}
inline int quickBitrateKbps(int mbps) { return mbps >= 1 && mbps <= 12 ? mbps * 1000 : 0; }
inline bool profileRangeSupported(const std::string &profile, const std::string &range)
{
	return profile != "main42210" || range != "Full";
}
// Explicit SDR input format; profile metadata alone never changes the OBS video pipeline.
inline std::string profileFormat(const std::string &profile)
{
	if (profile == "main") return "NV12";
	if (profile == "main10") return "P010";
	if (profile == "main42210") return "P216";
	return {};
}
} // namespace pixelview
