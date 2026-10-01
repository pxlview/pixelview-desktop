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
 * exactly sRGB. HDR: P010 or v210 only, labelled with the operator's transfer.
 * Neither is a grading transform. */
bool pixelview_video_info(GstCaps *caps, enum pixelview_color color, GstVideoInfo *info);
bool pixelview_video_frame(const GstVideoFrame *mapped, enum pixelview_color color, struct obs_source_frame2 *frame);
/* An eight-bit NV12 frame becomes ten-bit P010 holding exactly code * 4, in
 * *storage (grown as needed; the caller owns and g_frees it and keeps it alive
 * until the frame has been output). Other formats are left untouched. libobs
 * converts NV12 sources through an eight-bit RGB texture, and VideoToolbox
 * widens eight-bit video with a full-range gain (white 943 instead of 940). */
bool pixelview_video_widen_nv12(struct obs_source_frame2 *frame, uint8_t **storage, size_t *capacity);
/* NULL when the caps' colorimetry fits the mode, else one PV_COLOR_* reason. */
const char *pixelview_video_color_mismatch(GstCaps *caps, enum pixelview_color color);
/* Every stream is delivered in its own sampling and bit depth. The patched
 * vtdec picks v210 only for a 4:2:2 stream (an unpatched one would subsample
 * it to P010, which the capability probe rules out before 4:2:2 is offered),
 * NV12 for an eight-bit 4:2:0 stream and P010 for a ten-bit one. Eight-bit
 * video must not be decoded to P010: VideoToolbox widens it with a full-range
 * gain (white 235 becomes 943 rather than 940, neutral chroma 514). It is
 * decoded as NV12 and widened exactly by pixelview_video_widen_nv12 instead.
 * Upstream vtdec would pick NV12 for every stream from this list, so the
 * patch is required here too.
 * Compatibility BGRA and I422_10LE samples remain accepted by the adapter. */
#define PIXELVIEW_RECEIVE_RAW_CAPS "video/x-raw,format=(string){P010_10LE,v210,NV12}"
