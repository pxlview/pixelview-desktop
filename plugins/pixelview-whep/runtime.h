/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gst/gst.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Initialize once, before any endpoint can be supplied. Refuse a registry
 * initialized by another module: otherwise an external decoder could load. */
static bool pixelview_gst_init(void)
{
 if (gst_is_initialized()) return false;
 Dl_info module;
 if (!dladdr((void *)&pixelview_gst_init, &module)) return false;
 char *module_path = realpath(module.dli_fname, NULL);
 if (!module_path) return false;
 char *dir = g_path_get_dirname(module_path); free(module_path);
 char *relative = g_build_filename(dir,"..","Resources","GStreamer",NULL); g_free(dir);
 char *root = realpath(relative,NULL); g_free(relative);
 if (!root) return false;
 char *plugins = g_build_filename(root,"lib","gstreamer-1.0",NULL);
 char *scanner = g_build_filename(root,"libexec","gst-plugin-scanner",NULL);
 bool valid = g_file_test(plugins,G_FILE_TEST_IS_DIR) && g_file_test(scanner,G_FILE_TEST_IS_EXECUTABLE);
 if (valid) {
  g_setenv("GST_PLUGIN_SYSTEM_PATH", "", TRUE);
  g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);
  g_setenv("GST_PLUGIN_PATH", plugins, TRUE);
  g_setenv("GST_PLUGIN_PATH_1_0", plugins, TRUE);
  g_setenv("GST_PLUGIN_SCANNER", scanner, TRUE);
  g_setenv("GST_PLUGIN_SCANNER_1_0", scanner, TRUE);
  /* Don't import another process's registry or serialize runtime diagnostics. */
  g_setenv("GST_REGISTRY", "/dev/null", TRUE);
  g_setenv("GST_REGISTRY_1_0", "/dev/null", TRUE);
  g_setenv("GST_DEBUG", "0", TRUE);
  g_unsetenv("GST_DEBUG_FILE"); g_unsetenv("GST_DEBUG_DUMP_DOT_DIR");
  g_unsetenv("GST_PLUGIN_FEATURE_RANK"); g_unsetenv("GST_TRACERS");
  GError *error = NULL;
  valid = gst_init_check(NULL,NULL,&error);
  g_clear_error(&error);
  gst_debug_set_active(FALSE);
  const char *required[] = {"whepclientsrc","webrtcbin","nicesrc","nicesink","vtdec","opusdec","rtph264depay","rtph265depay","rtpopusdepay","h264parse","h265parse","appsink",NULL};
  for (int i=0; valid && required[i]; i++) {
   GstElementFactory *factory = gst_element_factory_find(required[i]);
   if (!factory) { valid = false; break; }
   GstPlugin *plugin = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory));
   const char *filename = plugin ? gst_plugin_get_filename(plugin) : NULL;
   valid = filename && g_str_has_prefix(filename,plugins) && filename[strlen(plugins)] == '/';
   if (plugin) gst_object_unref(plugin);
   gst_object_unref(factory);
  }
 }
 g_free(plugins); g_free(scanner); free(root);
 return valid;
}
