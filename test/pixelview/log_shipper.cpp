// Offline test of the production log shipping policy (no network or app build).
#include "frontend/utility/PixelviewLogShipper.hpp"
#include <QtCore/QTemporaryDir>
#include <cassert>
#include <cstdio>
using namespace pixelview;

static qint64 clockMs = 1790000000000;
static const QString device = QStringLiteral("707880.123e4567-e89b-12d3-a456-426614174000.") + QString(43, 'A');

static LogShipper shipper(const QString &spool = {})
{
	LogShipper s;
	s.launchId = QStringLiteral("launch-now");
	s.version = QStringLiteral("0.0.9");
	s.build = QStringLiteral("9");
	s.osVersion = QStringLiteral("26.0");
	s.cpu = QStringLiteral("Apple M1 Pro");
	s.hardwareModel = QStringLiteral("MacBookPro18,1");
	s.home = QStringLiteral("/Users/alice");
	s.spoolPath = spool;
	s.now = [] { return clockMs; };
	return s;
}

static std::deque<CapturedLine> lines(std::initializer_list<CapturedLine> list) { return {list}; }

static QJsonObject batchJson(const LogShipper &s, int *count = nullptr)
{
	const auto batch = s.nextBatch();
	if (count) *count = batch.count;
	return QJsonDocument::fromJson(batch.body).object();
}

static qint64 sent(const LogShipper &s) { return s.nextBatch().lastUid; }

static void roles_and_redaction()
{
	assert(LogShipper::role("[pixelview-whep] media stopped") == "receive");
	assert(LogShipper::role("[pixelview-receive] receiver ready: Receiving") == "receive");
	assert(LogShipper::role("[obs-webrtc] [whip_output: 'x'] connected") == "send");
	assert(LogShipper::role("==== Streaming Start ===============") == "send");
	assert(LogShipper::role("Pixelview: connecting control socket") == "app");

	// Only stream diagnostics are on the allowlist (lines from real logs).
	for (const char *line : {"[VideoToolbox advanced_video_stream: 'hevc']: settings:\n\tvt_encoder_id          x\n\tbitrate: 6000 (kbps)",
				 "[VideoToolbox encoder]: Added VideoToolbox encoders",
				 "[FFmpeg libopus encoder: 'adv_stream_audio'] bitrate: 160, channels: 2",
				 "[x264 encoder: 'advanced_video_stream'] settings:\n\trate_control: CBR",
				 "[CoreAudio AAC: 'adv_stream_audio']: settings:\n\tbitrate: 160", "[CoreAudio encoder]: Adding CoreAudio AAC encoders",
				 "Output 'adv_stream': Number of lagged frames due to rendering lag/stalls: 1 (0.0%)",
				 "video_thread(video): min=0.56 ms, median=1.159 ms", u8" \u2517receive_video: min=0.559 ms",
				 u8"      \u2523encode(advanced_video_stream): min=0.5 ms", u8"      \u2517send_packet: min=0 ms",
				 u8"    \u2517do_encode: min=0.559 ms", "[pixelview-send] x",
				 // The reason a stream did not start, stopped or degraded.
				 "Stream output type 'whip_output' failed to start!  Last Error: Connect failed: HTTP 404",
				 "Error encoding with encoder 'advanced_video_stream'", "creating encoder 'advanced_video_stream' (x) failed",
				 "Video stopped, number of skipped frames due to encoding lag: 12/3000 (0.4%)",
				 "obs-output 'adv_stream': Missing keyframe with pts 1 for encoder 'x' (group 0)"})
		assert(LogShipper::role(QString::fromUtf8(line)) == "send");
	for (const char *line : {"video settings reset:\n\tbase resolution:   1920x1080", "audio settings reset:\n\tsamples per sec: 48000",
				 "decklink: Stopping capture of 'UltraStudio 4K Mini'...", "decklink: Failed to enable video input",
				 "Decklink API Installed version 16.4", "Crash or unclean shutdown detected", "Max audio buffering reached!",
				 "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed",
				 "Source My Camera audio is lagging (over by 12.50 ms) at max audio buffering. Restarting source audio.",
				 "Pixelview FPS changed to 25/1 (1920x1080 canvas and output)", "[pixelview-logs] 3 log lines were dropped",
				 "adding 21 milliseconds of audio buffering, total audio buffering is now 21 milliseconds (source: My Camera)"})
		assert(LogShipper::role(QString::fromUtf8(line)) == "app");
	assert(LogShipper::role("Pixelview receive colour: SDR") == "receive");
	assert(LogShipper::role("[decklink-output-ui] Start failed: no device") == "receive");
	for (const char *line : {"[decklink] output video: 10-bit YUV (canvas P216, 203 nits)", "failed to create video frame 0x80004005",
				 "failed to schedule video frame for preroll 0x80004005", "No active audio"})
		assert(LogShipper::role(line) == "receive");
	for (const char *line : {"Switched to scene 'Client pitch'", "Loaded scenes:", "- scene 'Scene':", "    - source: 'Secret deck' (window_capture)",
				 "Audio monitoring device:\n\tname: Alice's AirPods\n\tid: x", "hotkeys-cocoa: Using keyboard layout 'com.apple.keylayout.ABC'",
				 "[Media Source 'clip']: settings:\n\tinput: /Volumes/Client/master.mov", "CPU Name: Apple M1 Pro",
				 "obs_graphics_thread(41.6667 ms): min=0.098 ms", u8" \u2523tick_sources: min=0 ms", u8"     \u2503 \u2523obs_init_module(decklink): 22.069 ms",
				 "encode(advanced_video_stream): not a profiler row", "[window-capture: 'Mail'] update", "Output", "", "adding source",
				 // libav's own messages can name a media file.
				 "[ffmpeg] /Volumes/Client/master.mov: No such file or directory", "Source My Camera activated"})
		assert(LogShipper::role(QString::fromUtf8(line)).isEmpty());
	assert(LogShipper::redact("adding 21 milliseconds of audio buffering (source: My Camera)", "/Users/alice") ==
	       "adding 21 milliseconds of audio buffering (source)");
	assert(LogShipper::redact("Source My Camera audio is lagging (over by 12.50 ms) at max audio buffering.", "/Users/alice") ==
	       "Source audio is lagging (over by 12.50 ms) at max audio buffering.");
	assert(LogShipper::level(100) == "error" && LogShipper::level(200) == "warning");
	assert(LogShipper::level(300) == "info" && LogShipper::level(400) == "debug");

	const QString raw = "token " + device + " Authorization: Bearer abc.def " +
			    "srt://in.pixelview.io:1234?passphrase=hunter2&latency=1 password=pw1 " +
			    "loaded /Users/alice/Library/foo https://api4.pixelview.io/707880/whep/1?token=zzz";
	const QString text = LogShipper::redact(raw, "/Users/alice");
	for (const char *secret : {"hunter2", "abc.def", "pw1", "zzz", "/Users/alice", "AAAAAAAAAA"})
		assert(!text.contains(QLatin1String(secret)));
	assert(text.contains("srt://in.pixelview.io:1234?[REDACTED]"));
	assert(text.contains("~/Library/foo"));
	assert(text.contains("https://api4.pixelview.io/707880/whep/1?[REDACTED]"));
	assert(LogShipper::redact(text, "/Users/alice") == text);

	// Windows: QDir::homePath() uses '/', log lines use '\\' in any case.
	const QString windows = LogShipper::redact("loaded C:\\users\\Alice\\AppData\\x and C:/Users/Alice/y", "C:/Users/Alice");
	assert(!windows.contains(QLatin1String("Alice"), Qt::CaseInsensitive));
	assert(windows == "loaded ~\\AppData\\x and ~/y");
	for (const char *line : {"[obs-nvenc: 'advanced_video_stream'] settings:\n\tcodec: HEVC", "[texture-amf-h265] Encoder: AMD",
				 "[qsv encoder: 'advanced_video_stream'] settings:", "[qsv encoder] Forcing main10 for P010"})
		assert(LogShipper::role(QString::fromUtf8(line)) == "send");
}

static void capture_is_thread_safe_bounded_and_switchable()
{
	auto &capture = LogCapture::instance();
	qint64 dropped = -1;
	capture.take(dropped);
	capture.setReceiveContext("sess-1", "viewer-1");
	for (size_t i = 0; i < LogCapture::MaxBuffered + 5; ++i) capture.capture(300, "[pixelview-whep] x");
	auto taken = capture.take(dropped);
	assert(taken.size() == LogCapture::MaxBuffered && dropped == 5);
	assert(taken.front().session == "sess-1" && taken.front().viewer == "viewer-1");
	assert(taken.back().seq > taken.front().seq);
	capture.setEnabled(false);
	capture.capture(100, "ignored");
	assert(capture.take(dropped).empty());
	capture.setEnabled(true);
	capture.setReceiveContext({}, {});
	assert(!capture.launchId.isEmpty());
}

static void ingest_and_batch()
{
	auto s = shipper();
	s.ingest(lines({{1, clockMs - 1000, 300, "Pixelview: paired", "sess-1", "v1"},
			{2, clockMs - 900, 200, "[pixelview-receive] media stopped", "sess-1", "v1"},
			{3, clockMs - 60 * 60 * 1000, 300, "Pixelview: too old", "", ""}}),
		 3);
	// The stale line is pruned; the capture drop becomes a visible warning.
	assert(s.pending.size() == 3);
	// A multi-line message (an encoder's settings block) is one entry and is
	// uploaded whole.
	auto block = shipper();
	const std::string settings = "[VideoToolbox advanced_video_stream: 'hevc']: settings:\n\tbitrate:               6000 (kbps)\n"
				     "\tkeyint:                2 (s)\n\tprofile:               main10\n";
	LogCapture::instance().capture(300, settings.c_str());
	qint64 none = 0;
	block.ingest(LogCapture::instance().take(none), none);
	const auto sentBlock = batchJson(block)["entries"].toArray()[0].toObject();
	assert(sentBlock["role"] == "send" && sentBlock["message"].toString() == QString::fromStdString(settings));
	assert(sentBlock["message"].toString().contains("keyint:                2 (s)"));
	// Lines off the allowlist are neither queued nor spooled.
	s.ingest(lines({{4, clockMs, 300, "Switched to scene 'Client pitch'", "", ""},
			{5, clockMs, 100, "[Media Source 'clip']: failed to open /Volumes/Client/master.mov", "", ""}}),
		 0);
	assert(s.pending.size() == 3 && s.urgentAt == 0);
	assert(s.pending.front().message.contains("3 log lines were dropped"));
	int count = 0;
	const auto body = batchJson(s, &count);
	assert(count == 3);
	assert(body["launch_id"] == "launch-now" && body["app_version"] == "0.0.9" && body["build"] == "9");
	assert(body["os_version"] == "26.0" && body["sent_at"].toInteger() == clockMs);
	assert(body["cpu"] == "Apple M1 Pro" && body["hardware_model"] == "MacBookPro18,1");
	const auto entries = body["entries"].toArray();
	const auto app = entries[1].toObject(), receive = entries[2].toObject();
	assert(app["role"] == "app" && !app.contains("session_id"));
	assert(receive["role"] == "receive" && receive["level"] == "warning");
	assert(receive["session_id"] == "sess-1" && receive["viewer_id"] == "v1");
	assert(receive["seq"].toInteger() == 2 && receive["ts"].toInteger() == clockMs - 900);

	// A context the backend would refuse (it answers 400 for the whole batch)
	// stays off the wire; the line itself is still sent.
	assert(validId("sess-1") && validId(QString(128, 'a')) && validId("A_b-9"));
	assert(!validId("") && !validId(QString(129, 'a')) && !validId("bad session#") && !validId("s\xc3\xa9"));
	auto odd = shipper();
	odd.ingest(lines({{1, clockMs, 300, "[pixelview-receive] receiver ready", "bad session#", "v 1"},
			  {2, clockMs, 300, "[pixelview-receive] receiver ready", "sess-2", "v 1"}}),
		   0);
	const auto oddEntries = batchJson(odd, &count)["entries"].toArray();
	assert(count == 2);
	assert(!oddEntries[0].toObject().contains("session_id") && !oddEntries[0].toObject().contains("viewer_id"));
	assert(oddEntries[1].toObject()["session_id"] == "sess-2" && !oddEntries[1].toObject().contains("viewer_id"));

	// Batches never mix launches and respect the entry and byte limits.
	auto big = shipper();
	for (int i = 0; i < LogShipper::MaxBatchEntries + 10; ++i)
		big.ingest(lines({{i, clockMs, 300, "Pixelview: line", "", ""}}), 0);
	big.pending.front().launch = "launch-old";
	batchJson(big, &count);
	assert(count == 1);
	big.pending.removeFirst();
	batchJson(big, &count);
	assert(count == LogShipper::MaxBatchEntries);
	auto wide = shipper();
	const std::string text = "Pixelview: " + std::string(LogCapture::MaxChars, 'x');
	for (int i = 0; i < 100; ++i) wide.ingest(lines({{i, clockMs, 300, text, "", ""}}), 0);
	const auto batch = wide.nextBatch();
	assert(batch.count > 0 && batch.count < 100 && batch.body.size() <= LogShipper::MaxBatchBytes);
}

static void scheduling_and_outcomes()
{
	auto s = shipper();
	const QString cred = "cred-a";
	assert(!s.due(cred)); // nothing pending
	s.ingest(lines({{1, clockMs, 300, "Pixelview: info", "", ""}}), 0);
	assert(!s.due({})); // no credential yet
	assert(s.due(cred)); // first upload is immediate
	s.inFlight = true;
	assert(!s.due(cred));
	assert(s.finished(202, 0, sent(s), cred) == LogShipper::Result::Sent);
	assert(s.pending.isEmpty() && !s.inFlight);

	// Info waits for the interval; an error goes out early, but never closer
	// than the floor to the previous upload.
	s.ingest(lines({{2, clockMs, 300, "Pixelview: info", "", ""}}), 0);
	assert(!s.due(cred));
	s.ingest(lines({{3, clockMs, 100, "Pixelview: boom", "", ""}}), 0);
	assert(!s.due(cred));
	clockMs += LogShipper::ErrorDelayMs;
	assert(!s.due(cred));
	clockMs += LogShipper::MinSpacingMs - LogShipper::ErrorDelayMs;
	assert(s.due(cred));
	assert(s.finished(202, 0, sent(s), cred) == LogShipper::Result::Sent && s.urgentAt == 0);
	// With no recent upload the error delay alone applies.
	clockMs += LogShipper::IntervalMs - 1000;
	s.ingest(lines({{4, clockMs, 100, "Pixelview: boom", "", ""}}), 0);
	assert(!s.due(cred));
	clockMs += LogShipper::ErrorDelayMs;
	assert(s.due(cred));
	assert(s.finished(202, 0, sent(s), cred) == LogShipper::Result::Sent);

	// 429/503 honour Retry-After; transport failures back off exponentially.
	s.ingest(lines({{5, clockMs, 300, "Pixelview: info", "", ""}}), 0);
	clockMs += LogShipper::IntervalMs;
	assert(s.due(cred));
	assert(s.finished(429, 120, sent(s), cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 120000);
	assert(s.pending.size() == 1);
	clockMs = s.nextAttempt;
	assert(s.finished(0, 0, sent(s), cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 10000);
	assert(s.finished(502, 0, sent(s), cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 20000);
	for (int i = 0; i < 10; ++i) s.finished(0, 0, sent(s), cred);
	assert(s.nextAttempt == clockMs + 300000);

	// A rejected credential pauses until the credential changes.
	clockMs = s.nextAttempt;
	assert(s.finished(401, 0, sent(s), cred) == LogShipper::Result::Paused);
	assert(!s.due(cred) && s.due("cred-b") && s.pending.size() == 1);
	// A backend without the endpoint is retried hourly; bad batches are dropped.
	assert(s.finished(404, 0, sent(s), "cred-b") == LogShipper::Result::Paused && s.nextAttempt == clockMs + 3600000);
	assert(s.finished(413, 0, sent(s), "cred-b") == LogShipper::Result::Discarded && s.pending.isEmpty());
}

// A steady error source keeps the early trigger armed. One tick a second for
// an hour must stay well inside the backend's 1800 requests an hour.
static void error_storm_respects_the_request_budget()
{
	auto s = shipper();
	const QString cred = "cred-a";
	int requests = 0;
	qint64 last = 0;
	for (int second = 0; second < 3600; ++second, clockMs += 1000) {
		s.ingest(lines({{second, clockMs, 100, "[obs-webrtc] send failed", "", ""}}), 0);
		if (!s.due(cred)) continue;
		assert(!last || clockMs - last >= LogShipper::MinSpacingMs);
		last = clockMs;
		++requests;
		assert(s.finished(202, 0, sent(s), cred) == LogShipper::Result::Sent);
	}
	assert(requests > 0 && requests <= 3600 * 1000 / LogShipper::MinSpacingMs && requests < 1800 / 2);
	// A backlog of full batches is paced the same way.
	auto full = shipper();
	for (int i = 0; i < 3 * LogShipper::MaxBatchEntries; ++i) full.ingest(lines({{i, clockMs, 300, "Pixelview: line", "", ""}}), 0);
	assert(full.due(cred));
	assert(full.finished(202, 0, sent(full), cred) == LogShipper::Result::Sent);
	assert(full.pending.size() == 2 * LogShipper::MaxBatchEntries && !full.due(cred));
	clockMs += LogShipper::MinSpacingMs;
	assert(full.due(cred));
}

// The queue loses lines from the front (age, overflow) while a request is in
// flight; the answer must only remove the lines that were in the batch.
static void answer_removes_only_the_lines_that_were_sent()
{
	QTemporaryDir dir;
	assert(dir.isValid());
	const QString path = dir.filePath("spool.ndjson");
	{
		auto old = shipper(path);
		old.launchId = "launch-old";
		old.ingest(lines({{1, clockMs - LogShipper::MaxAgeMs + 1000, 300, "Pixelview: old 1", "", ""},
				  {2, clockMs - LogShipper::MaxAgeMs + 1000, 300, "Pixelview: old 2", "", ""},
				  {3, clockMs, 300, "Pixelview: old 3", "", ""}}),
			   0);
		assert(old.saveSpool(true));
	}
	auto s = shipper();
	s.spoolPath = path;
	s.ingest(lines({{0, clockMs, 300, "Pixelview: new 1", "", ""}, {1, clockMs, 300, "Pixelview: new 2", "", ""}}), 0);
	s.loadSpool();
	assert(s.pending.size() == 5);
	const auto batch = s.nextBatch(); // The previous launch's three lines.
	assert(batch.count == 3 && batch.lastUid > 0);
	s.inFlight = true;
	// Two of them age out before the answer arrives.
	clockMs += 2000;
	s.ingest({}, 0);
	assert(s.pending.size() == 3);
	assert(s.finished(202, 0, batch.lastUid, "cred-a") == LogShipper::Result::Sent);
	assert(s.pending.size() == 2 && s.pending[0].message == "Pixelview: new 1" && s.pending[1].message == "Pixelview: new 2");
	// A late answer for lines that are no longer queued removes nothing.
	assert(s.finished(202, 0, batch.lastUid, "cred-a") == LogShipper::Result::Sent && s.pending.size() == 2);
	// Overflow drops the oldest; the batch still only removes its own lines.
	auto burst = shipper();
	for (int i = 0; i < LogShipper::MaxPending; ++i) burst.ingest(lines({{i, clockMs, 300, "Pixelview: line", "", ""}}), 0);
	const auto first = burst.nextBatch();
	assert(first.count == LogShipper::MaxBatchEntries);
	for (int i = 0; i < 100; ++i) burst.ingest(lines({{LogShipper::MaxPending + i, clockMs, 300, "Pixelview: line", "", ""}}), 0);
	assert(burst.pending.size() == LogShipper::MaxPending);
	burst.finished(202, 0, first.lastUid, "cred-a");
	assert(burst.pending.size() == LogShipper::MaxPending - (LogShipper::MaxBatchEntries - 100));
	assert(burst.pending.front().seq == LogShipper::MaxBatchEntries);
}

static void spool_survives_restart()
{
	QTemporaryDir dir;
	assert(dir.isValid());
	const QString path = dir.filePath("spool.ndjson");
	{
		auto s = shipper(path);
		s.ingest(lines({{7, clockMs, 200, "[pixelview-receive] stall", "sess-9", "v9"}}), 0);
		assert(s.saveSpool(true) && QFile::exists(path));
	}
	clockMs += 60 * 1000;
	auto next = shipper(path);
	next.launchId = "launch-next";
	next.ingest(lines({{0, clockMs, 300, "Pixelview: new launch", "", ""}}), 0);
	next.loadSpool();
	assert(next.pending.size() == 2);
	assert(next.pending.front().launch == "launch-now" && next.pending.front().session == "sess-9");
	int count = 0;
	const auto body = batchJson(next, &count);
	assert(count == 1 && body["launch_id"] == "launch-now");
	assert(body["entries"].toArray()[0].toObject()["session_id"] == "sess-9");
	// Lines too old for the backend are not kept.
	clockMs += LogShipper::MaxAgeMs;
	auto late = shipper(path);
	late.loadSpool();
	assert(late.pending.isEmpty());
	// Switching sharing off removes the spool and disowns a request in flight.
	const auto flight = next.flight;
	next.disable();
	assert(next.flight != flight && !next.inFlight);
	assert(next.pending.isEmpty() && !QFile::exists(path));
	assert(next.saveSpool(true));
}

int main()
{
	roles_and_redaction();
	capture_is_thread_safe_bounded_and_switchable();
	ingest_and_batch();
	scheduling_and_outcomes();
	error_storm_respects_the_request_budget();
	answer_removes_only_the_lines_that_were_sent();
	spool_survives_restart();
	std::puts("log shipper ok");
	return 0;
}
