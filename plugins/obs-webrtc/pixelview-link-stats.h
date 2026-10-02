#pragma once
// Pixelview modification, 2026-10-02: link statistics for a WHIP stream.
//
// The media server already answers every RTCP sender report with a receiver
// report on the media path itself. Timing that exchange gives the round trip
// to the server the stream is published to, and the report carries the loss
// and jitter the server measured. Nothing extra is sent on the wire.
//
// Pure byte parsing with no dependencies, so it is tested on its own
// (test/pixelview/link_stats_native.cpp).
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace pixelview {

struct LinkSnapshot {
	bool reported = false; // A receiver report for this stream has arrived.
	int64_t age_ms = -1;   // Since the last receiver report.
	int rtt_ms = -1;       // Smoothed round trip; -1 until one was measured.
	double loss_pct = 0;   // Lost in the last report interval.
	double jitter_ms = 0;  // Interarrival jitter at the server.
	uint32_t lost = 0;     // Cumulative packets lost.
	uint64_t nacked = 0;   // Cumulative packets the server asked to have resent.
};

class LinkStats {
public:
	LinkStats(uint32_t ssrc, uint32_t clockRate) : ssrc(ssrc), clockRate(clockRate ? clockRate : 90000) {}

	// RTCP this side sends: remembers when each sender report left.
	void sent(const uint8_t *data, size_t size, int64_t nowUs)
	{
		walk(data, size, [&](uint8_t type, uint8_t, const uint8_t *body, size_t length) {
			if (type != 200 || length < 12 || be32(body) != ssrc) return;
			const uint32_t middle = (be32(body + 4) << 16) | (be32(body + 8) >> 16);
			std::lock_guard<std::mutex> lock(mutex);
			reports[next] = {middle, nowUs, true};
			next = (next + 1) % reports.size();
		});
	}

	// RTCP from the server: receiver reports (also inside its sender reports) and NACKs.
	void received(const uint8_t *data, size_t size, int64_t nowUs)
	{
		walk(data, size, [&](uint8_t type, uint8_t count, const uint8_t *body, size_t length) {
			if (type == 200 || type == 201) {
				const size_t skip = type == 200 ? 24 : 4;
				for (size_t i = 0; i < count && skip + (i + 1) * 24 <= length; ++i)
					block(body + skip + i * 24, nowUs);
			} else if (type == 205 && count == 1 && length >= 8 && be32(body + 4) == ssrc) {
				uint64_t packets = 0;
				for (size_t at = 8; at + 4 <= length; at += 4) {
					uint16_t mask = (uint16_t)((body[at + 2] << 8) | body[at + 3]);
					for (++packets; mask; mask &= (uint16_t)(mask - 1)) ++packets;
				}
				std::lock_guard<std::mutex> lock(mutex);
				nacked += packets;
			}
		});
	}

	LinkSnapshot snapshot(int64_t nowUs) const
	{
		std::lock_guard<std::mutex> lock(mutex);
		LinkSnapshot result;
		if (reportedAt < 0) {
			result.nacked = nacked;
			return result;
		}
		result.reported = true;
		result.age_ms = (nowUs - reportedAt) / 1000;
		result.rtt_ms = rttUs < 0 ? -1 : (int)((rttUs + 500) / 1000);
		result.loss_pct = fraction * 100.0 / 256.0;
		result.jitter_ms = jitter * 1000.0 / clockRate;
		result.lost = lost;
		result.nacked = nacked;
		return result;
	}

private:
	struct Report {
		uint32_t middle = 0;
		int64_t sentUs = 0;
		bool used = false;
	};

	static uint32_t be32(const uint8_t *p)
	{
		return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
	}

	// Calls each packet of a compound RTCP datagram with the body after its
	// four-byte header. Stops at the first packet that is not whole.
	template<typename Packet> static void walk(const uint8_t *data, size_t size, Packet packet)
	{
		for (size_t at = 0; data && at + 4 <= size;) {
			const size_t length = ((size_t)((data[at + 2] << 8) | data[at + 3]) + 1) * 4;
			if ((data[at] >> 6) != 2 || length > size - at) return;
			packet(data[at + 1], data[at] & 0x1f, data + at + 4, length - 4);
			at += length;
		}
	}

	void block(const uint8_t *b, int64_t nowUs)
	{
		if (be32(b) != ssrc) return;
		std::lock_guard<std::mutex> lock(mutex);
		reportedAt = nowUs;
		fraction = b[4];
		// Cumulative loss is a signed 24-bit count; duplicates can make it negative.
		const int32_t total = (int32_t)(((uint32_t)b[5] << 24) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 8)) >> 8;
		lost = total > 0 ? (uint32_t)total : 0;
		jitter = be32(b + 12);
		const uint32_t lastReport = be32(b + 16), delay = be32(b + 20);
		if (!lastReport) return; // The server has not seen a sender report yet.
		for (const Report &report : reports) {
			if (!report.used || report.middle != lastReport) continue;
			// The delay is how long the server held the report, in 1/65536 s.
			const int64_t sample = nowUs - report.sentUs - (int64_t)delay * 1000000 / 65536;
			// Clock granularity can put a loopback sample just below zero.
			if (sample < -50000) return;
			const int64_t value = sample < 0 ? 0 : sample;
			rttUs = rttUs < 0 ? value : rttUs + (value - rttUs) / 4;
			return;
		}
	}

	const uint32_t ssrc, clockRate;
	mutable std::mutex mutex;
	std::array<Report, 8> reports{};
	size_t next = 0;
	int64_t reportedAt = -1, rttUs = -1;
	uint32_t fraction = 0, lost = 0, jitter = 0;
	uint64_t nacked = 0;
};

} // namespace pixelview
