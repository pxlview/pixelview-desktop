/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_MAIN422_25P_H
#define PIXELVIEW_MAIN422_25P_H
#include <gst/gst.h>
/* Enabled for the user-authorized 1080p25 hardware test in normal builds.
 * Strict native format/range/timing checks remain enforced. This is not
 * physical-output certification. */
static inline gboolean pv_main422_25p_enabled(void)
{
 return TRUE;
}
#endif
