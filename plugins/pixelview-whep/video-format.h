/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <obs.h>
#include <gst/video/video.h>
/* The operator's receive colour mode, fixed per connection. SDR is limited
 * BT.709; PQ/HLG are limited BT.2020 with the chosen transfer. */
enum pixelview_color { PIXELVIEW_COLOR_SDR, PIXELVIEW_COLOR_PQ, PIXELVIEW_COLOR_HLG };
/* Canonical get_status failure reasons for a colour-mode mismatch. */
#define PV_COLOR_HDR_SOURCE "hdr-source-needs-hdr-receive"
#define PV_COLOR_SDR_SOURCE "sdr-source-in-hdr-receive"
#define PV_COLOR_TRANSFER_MISMATCH "hdr-transfer-mismatch"
#define PV_COLOR_UNSUPPORTED "unsupported-colorimetry"
/* Borrowed planes: caller must output/copy before unmapping.
 * SDR: exact limited BT.709 (or sRGB BGRA). DEFAULT deliberately uses OBS's
 * existing SDR processing, not an assertion that BT709's transfer curve is
 * exactly sRGB. HDR: P010 only, labelled with the operator's transfer. Neither
 * is a grading transform. */
bool pixelview_video_info(GstCaps *caps, enum pixelview_color color, GstVideoInfo *info);
bool pixelview_video_frame(const GstVideoFrame *mapped, enum pixelview_color color, struct obs_source_frame2 *frame);
/* NULL when the caps' colorimetry fits the mode, else one PV_COLOR_* reason. */
const char *pixelview_video_color_mismatch(GstCaps *caps, enum pixelview_color color);
/* vtdec fixates its own NV12 preference even when P010 is first in a list.
 * Require P010 on the production decoder branch, including eight-bit input
 * (upconversion, not a claim of original ten-bit precision). Compatibility
 * NV12/BGRA and genuine I422_10LE samples remain accepted by the adapter. */
#define PIXELVIEW_RECEIVE_RAW_CAPS "video/x-raw,format=P010_10LE"
