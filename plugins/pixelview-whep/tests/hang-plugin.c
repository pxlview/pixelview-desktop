/* SPDX-License-Identifier: GPL-2.0-or-later
 * A plugin whose init sleeps PV_TEST_HANG_SECONDS (unset or 0: never
 * returns), for run-runtime-lazy.py. */
#define PACKAGE "pixelview-test"
#include <gst/gst.h>
#include <stdlib.h>
#include <unistd.h>
static gboolean plugin_init(GstPlugin *plugin)
{
 (void)plugin;
 const char *value = getenv("PV_TEST_HANG_SECONDS");
 const int seconds = value ? atoi(value) : 0;
 if (seconds > 0) { sleep((unsigned)seconds); return TRUE; }
 for (;;) sleep(60);
}
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zzhang, "hangs in plugin_init",
                  plugin_init, "1.0", "LGPL", "pixelview-test", "https://pixelview.io")
