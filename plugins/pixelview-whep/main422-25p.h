/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_MAIN422_25P_H
#define PIXELVIEW_MAIN422_25P_H
#include <gst/gst.h>
/* Single switch for HEVC Main 4:2:2 10 reception. FALSE: normal builds neither
 * offer profile-id 4 nor admit a parsed main-422-10 stream; the route selector
 * refuses it with a typed "unsupported profile" error that the frontend turns
 * into "use HEVC Main or Main10" guidance. TRUE restores the earlier
 * user-authorized 1080p25 native path (strict format/range/timing checks, not
 * physical-output certification). Tests that exercise the native branch admit
 * it explicitly on the filter instead of depending on this policy. */
static inline gboolean pv_main422_25p_enabled(void)
{
 return FALSE;
}
#endif
