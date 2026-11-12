// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The session watermark burned into the sent picture (docs/features.md).
//
// The engine overlays the session watermark PNG (1920x1080 RGBA) scaled to
// the frame height at the bottom right, and that forces a transcode. Pixelview
// Desktop draws the same PNG the same way over its program picture and tells
// the engine with the WHIP header X-Pixelview-Watermark: sender; an engine
// that knows the header echoes it and leaves the picture (and passthrough)
// alone. Without the echo, or without a known watermark state when the stream
// starts, the Desktop draws nothing and the engine watermarks as before.
#include <obs.hpp>
#include <graphics/graphics.h>
#include <QtCore/QString>
#include <mutex>

namespace pixelview {

struct WatermarkPolicy {
	// A new stream may declare the sender watermark only when it knows the
	// session's state and, when it is on, already holds that exact image.
	static bool declare(bool known, bool enabled, const QString &id, const QString &loaded)
	{
		return known && (!enabled || (!id.isEmpty() && id == loaded));
	}
	// Drawn only for a stream that declared it (and was not refused). A changed
	// image replaces the previous one once fetched; until then the previous
	// one stays, so the stream is never without a watermark that is on.
	static bool show(bool declared, bool known, bool enabled, const QString &loaded)
	{
		return declared && known && enabled && !loaded.isEmpty();
	}
	// Ids name images in a file name: lowercase hex only.
	static bool validId(const QString &id)
	{
		if (id.isEmpty() || id.size() > 64)
			return false;
		for (const QChar c : id)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		return true;
	}
};

// Draws a private image source over the finished main mix (a libobs main
// rendered callback), so it reaches the encoder and the preview but never the
// saved scene. Set from the UI thread, drawn on the graphics thread.
class WatermarkOverlay {
	std::mutex mutex;
	OBSSource image;
	bool shown = false;

public:
	// Returns the previous image so the caller releases it outside the lock.
	OBSSource set(OBSSource next)
	{
		std::lock_guard<std::mutex> lock(mutex);
		OBSSource previous = image;
		image = next;
		return previous;
	}
	void show(bool on)
	{
		std::lock_guard<std::mutex> lock(mutex);
		shown = on;
	}
	bool showing()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return shown && image;
	}
	static void rendered(void *param)
	{
		auto *self = static_cast<WatermarkOverlay *>(param);
		std::lock_guard<std::mutex> lock(self->mutex);
		if (!self->shown || !self->image)
			return;
		const uint32_t cx = obs_source_get_width(self->image);
		const uint32_t cy = obs_source_get_height(self->image);
		// render_main_texture leaves the viewport at the canvas size.
		gs_rect canvas = {};
		gs_get_viewport(&canvas);
		if (!cx || !cy || canvas.cx <= 0 || canvas.cy <= 0)
			return;
		// As the engine: scaled to the frame height, aspect kept, bottom right.
		const float scale = (float)canvas.cy / (float)cy;
		gs_matrix_push();
		gs_matrix_identity();
		gs_matrix_translate3f((float)canvas.cx - (float)cx * scale, 0.0f, 0.0f);
		gs_matrix_scale3f(scale, scale, 1.0f);
		// libobs converts the sRGB image to the canvas space (SDR white in HDR).
		obs_source_video_render(self->image);
		gs_matrix_pop();
	}
};

} // namespace pixelview
