/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_MAIN422_25P_H
#define PIXELVIEW_MAIN422_25P_H
#include <gst/gst.h>
/* Switch for the dormant NATIVE HEVC Main 4:2:2 10 path (own VideoToolbox
 * decoder, v210 feed straight to DeckLink, strict 1080p25). FALSE: normal
 * builds receive Main 4:2:2 10 on the ordinary route instead, as v210 from the
 * patched vtdec, and only when the capability probe decoded it; otherwise
 * profile-id 4 is not offered and a parsed main-422-10 stream is refused with a
 * typed "unsupported profile" error. TRUE restores the earlier user-authorized
 * native path (never certified on a physical output). Tests that exercise the
 * native branch admit it explicitly on the filter instead of this policy. */
static inline gboolean pv_main422_25p_enabled(void)
{
 return FALSE;
}
#endif
