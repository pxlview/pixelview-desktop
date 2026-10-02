#pragma once
// Pixelview modification, 2026-10-02: feeds pixelview::LinkStats from a track.
//
// Takes the sender-report handler's place in the track's handler chain and
// runs it itself, because that handler emits its reports straight through
// the send callback, where no other handler in the chain would see them.
#include "pixelview-link-stats.h"

#include <rtc/rtc.hpp>

#include <chrono>
#include <memory>

namespace pixelview {

class LinkProbe final : public rtc::MediaHandler {
public:
	LinkProbe(std::shared_ptr<rtc::RtcpSrReporter> reporter, std::shared_ptr<LinkStats> stats)
		: reporter(std::move(reporter)),
		  stats(std::move(stats))
	{
	}

	static int64_t nowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			       std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	void incoming(rtc::message_vector &messages, const rtc::message_callback &) override
	{
		for (const auto &message : messages) {
			if (message && message->type == rtc::Message::Control)
				stats->received((const uint8_t *)message->data(), message->size(), nowUs());
		}
	}

	void outgoing(rtc::message_vector &messages, const rtc::message_callback &send) override
	{
		reporter->outgoing(messages, [&](rtc::message_ptr message) {
			if (message && message->type == rtc::Message::Control)
				stats->sent((const uint8_t *)message->data(), message->size(), nowUs());
			send(std::move(message));
		});
	}

private:
	const std::shared_ptr<rtc::RtcpSrReporter> reporter;
	const std::shared_ptr<LinkStats> stats;
};

} // namespace pixelview
