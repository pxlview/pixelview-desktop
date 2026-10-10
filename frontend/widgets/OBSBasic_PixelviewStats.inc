// Pixelview Desktop connection report (DESKTOP_STATS on the control socket).
//
// Every two seconds while streaming the Desktop reports the bitrate it sent
// and what the media server reports back over RTCP (round trip, loss, jitter),
// how hard this computer works (the Stats window's CPU, memory, frame rate and
// missed/skipped frames), and the round trip of the control socket itself. Idle, it reports
// only when the control round trip changed. The backend keeps the last report
// with the Desktop's presence and forwards it to the node's admins. A summary
// goes to the application log once a minute of streaming.
void OBSBasic::InitPixelviewStats()
{
	pixelviewStatsTimer = new QTimer(this);
	pixelviewStatsTimer->setInterval(pixelview::LinkReport::INTERVAL_MS);
	connect(pixelviewStatsTimer, &QTimer::timeout, this, &OBSBasic::PushPixelviewStats);
	pixelviewStatsTimer->start();
}

void OBSBasic::PushPixelviewStats()
{
	if (isClosing() || pixelviewShutdownPending) {
		pixelviewStatsTimer->stop();
		return;
	}
	if (pixelviewUnpairPending || !pixelviewDesktop) return;
	obs_output_t *output = outputHandler ? outputHandler->streamOutput.Get() : nullptr;
	const bool streaming = pixelviewActualStreaming && PixelviewLeaseValid() && output && obs_output_active(output);
	pixelview::MediaLink link;
	pixelview::HostLoad host;
	quint64 bytes = 0;
	if (streaming) {
		// The same sources as the Stats window; skipped frames come from the stream encoder's own video.
		obs_encoder_t *encoder = obs_output_get_video_encoder(output);
		video_t *video = encoder ? obs_encoder_video(encoder) : nullptr;
		if (!video) video = obs_get_video();
		host.valid = true;
		host.cpuPct = GetCPUUsage();
		host.memoryMb = (double)os_get_proc_resident_size() / (1024.0 * 1024.0);
		host.fps = obs_get_active_fps();
		host.frameTimeMs = (double)obs_get_average_frame_time_ns() / 1000000.0;
		host.rendered = obs_get_total_frames();
		host.missed = obs_get_lagged_frames();
		host.encoded = video_output_get_total_frames(video);
		host.skipped = video_output_get_skipped_frames(video);
		bytes = obs_output_get_total_bytes(output);
		calldata_t cd = {};
		// Only the WHIP output answers; any other output reports the bitrate alone.
		if (proc_handler_call(obs_output_get_proc_handler(output), "pixelview_link_stats", &cd)) {
			link.reported = calldata_bool(&cd, "reported");
			link.ageMs = (int)calldata_int(&cd, "age_ms");
			link.rttMs = (int)calldata_int(&cd, "rtt_ms");
			link.lossPct = calldata_float(&cd, "loss_pct");
			link.jitterMs = calldata_float(&cd, "jitter_ms");
			link.lost = calldata_int(&cd, "lost");
			link.nacked = calldata_int(&cd, "nacked");
		}
		calldata_free(&cd);
	}
	const qint64 now = pixelviewClock.elapsed();
	// Sampled through a control reconnect too, so the bitrate baseline and the log summary continue.
	const QJsonObject stats = pixelviewLinkReport.sample(now, pixelviewDesktop->controlRttMs, streaming, bytes, link, host);
	const QString line = pixelviewLinkReport.logLine(now);
	if (!line.isEmpty()) blog(LOG_INFO, "%s", QT_TO_UTF8(line));
	if (!pixelviewLease.ready || pixelviewClosingSocket) return;
	if (pixelviewLinkReport.take(stats))
		pixelviewLease.send({{"message", "DESKTOP_STATS"}, {"data", QJsonObject{{"stats", stats}}}});
}
