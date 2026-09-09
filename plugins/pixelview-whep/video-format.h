/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <obs.h>
#include <gst/video/video.h>
/* Borrowed planes: caller must output/copy before unmapping. SDR709 only.
 * DEFAULT deliberately uses OBS's existing SDR processing, not an assertion
 * that BT709's transfer curve is exactly sRGB. Not a grading transform. */
bool pixelview_video_info(GstCaps *caps, GstVideoInfo *info);
bool pixelview_video_frame(const GstVideoFrame *mapped, struct obs_source_frame2 *frame);
/* vtdec fixates its own NV12 preference even when P010 is first in a list.
 * Require P010 on the production decoder branch, including eight-bit input
 * (upconversion, not a claim of original ten-bit precision). Compatibility
 * NV12/BGRA and genuine I422_10LE samples remain accepted by the adapter. */
#define PIXELVIEW_RECEIVE_RAW_CAPS "video/x-raw,format=P010_10LE"
