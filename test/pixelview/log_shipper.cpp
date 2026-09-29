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

static void roles_and_redaction()
{
	assert(LogShipper::role("[pixelview-whep] media stopped") == "receive");
	assert(LogShipper::role("[pixelview-receive] receiver ready: Receiving") == "receive");
	assert(LogShipper::role("[obs-webrtc] [whip_output: 'x'] connected") == "send");
	assert(LogShipper::role("==== Streaming Start ===============") == "send");
	assert(LogShipper::role("Pixelview: connecting control socket") == "app");
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
			{3, clockMs - 60 * 60 * 1000, 300, "too old", "", ""}}),
		 3);
	// The stale line is pruned; the capture drop becomes a visible warning.
	assert(s.pending.size() == 3);
	assert(s.pending.front().message.contains("3 log lines were dropped"));
	int count = 0;
	const auto body = batchJson(s, &count);
	assert(count == 3);
	assert(body["launch_id"] == "launch-now" && body["app_version"] == "0.0.9" && body["build"] == "9");
	assert(body["os_version"] == "26.0" && body["sent_at"].toInteger() == clockMs);
	const auto entries = body["entries"].toArray();
	const auto app = entries[1].toObject(), receive = entries[2].toObject();
	assert(app["role"] == "app" && !app.contains("session_id"));
	assert(receive["role"] == "receive" && receive["level"] == "warning");
	assert(receive["session_id"] == "sess-1" && receive["viewer_id"] == "v1");
	assert(receive["seq"].toInteger() == 2 && receive["ts"].toInteger() == clockMs - 900);

	// Batches never mix launches and respect the entry and byte limits.
	auto big = shipper();
	for (int i = 0; i < LogShipper::MaxBatchEntries + 10; ++i)
		big.ingest(lines({{i, clockMs, 300, "line", "", ""}}), 0);
	big.pending.front().launch = "launch-old";
	batchJson(big, &count);
	assert(count == 1);
	big.pending.removeFirst();
	batchJson(big, &count);
	assert(count == LogShipper::MaxBatchEntries);
	auto wide = shipper();
	const std::string text(LogCapture::MaxChars, 'x');
	for (int i = 0; i < 100; ++i) wide.ingest(lines({{i, clockMs, 300, text, "", ""}}), 0);
	const auto batch = wide.nextBatch();
	assert(batch.count > 0 && batch.count < 100 && batch.body.size() <= LogShipper::MaxBatchBytes);
}

static void scheduling_and_outcomes()
{
	auto s = shipper();
	const QString cred = "cred-a";
	assert(!s.due(cred)); // nothing pending
	s.ingest(lines({{1, clockMs, 300, "info", "", ""}}), 0);
	assert(!s.due({})); // no credential yet
	assert(s.due(cred)); // first upload is immediate
	s.inFlight = true;
	assert(!s.due(cred));
	assert(s.finished(202, 0, 1, cred) == LogShipper::Result::Sent);
	assert(s.pending.isEmpty() && !s.inFlight);

	// Info waits for the interval; an error goes out after a short delay.
	s.ingest(lines({{2, clockMs, 300, "info", "", ""}}), 0);
	assert(!s.due(cred));
	s.ingest(lines({{3, clockMs, 100, "boom", "", ""}}), 0);
	assert(!s.due(cred));
	clockMs += LogShipper::ErrorDelayMs;
	assert(s.due(cred));
	int count = 0;
	batchJson(s, &count);
	assert(s.finished(202, 0, count, cred) == LogShipper::Result::Sent && s.urgentAt == 0);

	// 429/503 honour Retry-After; transport failures back off exponentially.
	s.ingest(lines({{4, clockMs, 300, "info", "", ""}}), 0);
	clockMs += LogShipper::IntervalMs;
	assert(s.due(cred));
	assert(s.finished(429, 120, 1, cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 120000);
	assert(s.pending.size() == 1);
	clockMs = s.nextAttempt;
	assert(s.finished(0, 0, 1, cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 10000);
	assert(s.finished(502, 0, 1, cred) == LogShipper::Result::Retry && s.nextAttempt == clockMs + 20000);
	for (int i = 0; i < 10; ++i) s.finished(0, 0, 1, cred);
	assert(s.nextAttempt == clockMs + 300000);

	// A rejected credential pauses until the credential changes.
	clockMs = s.nextAttempt;
	assert(s.finished(401, 0, 1, cred) == LogShipper::Result::Paused);
	assert(!s.due(cred) && s.due("cred-b") && s.pending.size() == 1);
	// A backend without the endpoint is retried hourly; bad batches are dropped.
	assert(s.finished(404, 0, 1, "cred-b") == LogShipper::Result::Paused && s.nextAttempt == clockMs + 3600000);
	assert(s.finished(413, 0, 1, "cred-b") == LogShipper::Result::Discarded && s.pending.isEmpty());
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
	next.ingest(lines({{0, clockMs, 300, "new launch", "", ""}}), 0);
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
	// Switching sharing off removes the spool.
	next.disable();
	assert(next.pending.isEmpty() && !QFile::exists(path));
	assert(next.saveSpool(true));
}

int main()
{
	roles_and_redaction();
	capture_is_thread_safe_bounded_and_switchable();
	ingest_and_batch();
	scheduling_and_outcomes();
	spool_survives_restart();
	std::puts("log shipper ok");
	return 0;
}
