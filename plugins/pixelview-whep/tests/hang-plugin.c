/* SPDX-License-Identifier: GPL-2.0-or-later
 * A plugin whose init never returns, for run-runtime-lazy.py. */
#define PACKAGE "pixelview-test"
#include <gst/gst.h>
#include <unistd.h>
static gboolean plugin_init(GstPlugin *plugin) { (void)plugin; for (;;) sleep(60); return TRUE; }
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, zzhang, "hangs in plugin_init",
                  plugin_init, "1.0", "LGPL", "pixelview-test", "https://pixelview.io")
