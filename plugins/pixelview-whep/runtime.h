/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <obs-module.h>
#include <gst/gst.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The bundled GStreamer runtime starts lazily, on its own thread, the first
 * time a receive needs it; never during module load or on the UI thread. A
 * Mac where it fails or hangs loses receiving only: the app still opens and
 * streams. GStreamer cannot be interrupted, so a hung load keeps its thread;
 * every later receive waits a bounded time and then fails.
 *
 * Nothing is scanned and the forked gst-plugin-scanner is never run (on one
 * customer Mac the app and the scanner deadlocked, each blocked in write() on
 * a full pipe to the other with neither reading, and GStreamer has no timeout,
 * so the app hung on the main thread at launch). Each plugin file in the
 * bundled directory is loaded in-process, by name, so the log names a plugin
 * that is slow or hangs. The directory is curated: a file that is not a
 * regular file or does not load fails the runtime. No system, user or
 * external registry is read; every required factory must come from the
 * bundled directory. */

#define PIXELVIEW_GST_WAIT_MS 60000u /* one receive attempt waits this long */
#ifndef PIXELVIEW_GST_BUDGET_MS
#define PIXELVIEW_GST_BUDGET_MS 90000u /* a load still running after this is reported as unavailable */
#endif
#define PIXELVIEW_GST_SLOW_MS 2000u        /* a load slower than this is logged as a warning */
#define PIXELVIEW_GST_SLOW_PLUGIN_MS 250u  /* a plugin slower than this is named in the log */

/* STALLED is never stored: it is LOADING past the overall budget, returned so
 * the receive fails with a typed reason. A load that finishes later still
 * makes the next receive READY. */
enum pixelview_gst_state { PIXELVIEW_GST_IDLE, PIXELVIEW_GST_LOADING, PIXELVIEW_GST_READY, PIXELVIEW_GST_FAILED, PIXELVIEW_GST_STALLED };

static GMutex pv_gst_lock; /* static storage: needs no init */
static GCond pv_gst_cond;
static enum pixelview_gst_state pv_gst_state; /* pv_gst_lock; never STALLED */
static gint64 pv_gst_started;                 /* pv_gst_lock; monotonic us */
static char pv_gst_current[96];               /* pv_gst_lock; plugin file being loaded */
static char *pv_gst_plugins;                  /* set once by configure, then read-only */

/* Module load, main thread, before any receive thread exists: resolve the
 * bundled runtime and set its environment. Makes no GStreamer call apart
 * from refusing a runtime another module already initialized. */
static bool pixelview_gst_configure(void)
{
 if (pv_gst_plugins) return true;
 if (gst_is_initialized()) return false;
 Dl_info module;
 if (!dladdr((void *)&pixelview_gst_configure, &module)) return false;
 char *module_path = realpath(module.dli_fname, NULL);
 if (!module_path) return false;
 char *dir = g_path_get_dirname(module_path); free(module_path);
 char *relative = g_build_filename(dir,"..","Resources","GStreamer",NULL); g_free(dir);
 char *root = realpath(relative,NULL); g_free(relative);
 if (!root) return false;
 char *plugins = g_build_filename(root,"lib","gstreamer-1.0",NULL);
 char *scanner = g_build_filename(root,"libexec","gst-plugin-scanner",NULL);
 free(root);
 if (!g_file_test(plugins,G_FILE_TEST_IS_DIR)) { g_free(plugins); g_free(scanner); return false; }
 /* No search path at all: plugins are loaded one by one from `plugins`. */
 g_setenv("GST_PLUGIN_SYSTEM_PATH", "", TRUE);
 g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);
 g_setenv("GST_PLUGIN_PATH", "", TRUE);
 g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);
 /* Never fork the scanner. Should anything re-enable it, use only ours. */
 g_setenv("GST_REGISTRY_FORK", "no", TRUE);
 g_setenv("GST_PLUGIN_SCANNER", scanner, TRUE);
 g_setenv("GST_PLUGIN_SCANNER_1_0", scanner, TRUE);
 /* Don't import another process's registry or serialize runtime diagnostics. */
 g_setenv("GST_REGISTRY", "/dev/null", TRUE);
 g_setenv("GST_REGISTRY_1_0", "/dev/null", TRUE);
 g_setenv("GST_REGISTRY_UPDATE", "no", TRUE);
 g_setenv("GST_DEBUG", "0", TRUE);
 g_unsetenv("GST_DEBUG_FILE"); g_unsetenv("GST_DEBUG_DUMP_DOT_DIR");
 g_unsetenv("GST_PLUGIN_FEATURE_RANK"); g_unsetenv("GST_TRACERS");
 g_free(scanner);
 pv_gst_plugins = plugins;
 return true;
}

static gint pv_gst_compare_names(gconstpointer a, gconstpointer b)
{
 return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Loader thread only. Failures name the plugin file, never its full path
 * (the bundle can live under the user's home folder). */
static bool pv_gst_load(char **reason)
{
 if (gst_is_initialized()) { *reason = g_strdup("GStreamer was already initialized by another module"); return false; }
 gst_registry_fork_set_enabled(FALSE);
 /* GStreamer's SIGSEGV handler is process-wide while it is installed: an
  * unrelated crash would be blamed on a plugin and exit without a report. */
 gst_segtrap_set_enabled(FALSE);
 GError *error = NULL;
 bool valid = gst_init_check(NULL,NULL,&error);
 g_clear_error(&error);
 if (!valid) { *reason = g_strdup("gst_init_check failed"); return false; }
 gst_debug_set_active(FALSE);
 GDir *dir = g_dir_open(pv_gst_plugins, 0, NULL);
 if (!dir) { *reason = g_strdup("bundled plugin directory unreadable"); return false; }
 GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
 for (const char *name; (name = g_dir_read_name(dir));)
  if (g_str_has_prefix(name, "libgst") && g_str_has_suffix(name, ".dylib")) g_ptr_array_add(names, g_strdup(name));
 g_dir_close(dir);
 g_ptr_array_sort(names, pv_gst_compare_names);
 for (guint i = 0; valid && i < names->len; i++) {
  const char *name = g_ptr_array_index(names, i);
  g_mutex_lock(&pv_gst_lock); g_strlcpy(pv_gst_current, name, sizeof pv_gst_current); g_mutex_unlock(&pv_gst_lock);
  char *path = g_build_filename(pv_gst_plugins, name, NULL);
  /* A symlink would pass the origin check below with a foreign target. */
  struct stat info;
  if (lstat(path, &info) || !S_ISREG(info.st_mode)) {
   *reason = g_strdup_printf("bundled plugin %s is not a regular file", name);
   valid = false;
  } else {
   const gint64 start = g_get_monotonic_time();
   GstPlugin *plugin = gst_plugin_load_file(path, &error);
   const long long ms = (long long)((g_get_monotonic_time() - start) / 1000);
   if (!plugin) {
    *reason = g_strdup_printf("bundled plugin %s did not load (%s %d)", name,
                              error ? g_quark_to_string(error->domain) : "unknown", error ? error->code : 0);
    valid = false;
   } else {
    gst_object_unref(plugin);
   }
   if (ms >= PIXELVIEW_GST_SLOW_PLUGIN_MS) blog(LOG_INFO, "[pixelview-whep] GStreamer plugin %s took %lld ms to load", name, ms);
  }
  g_clear_error(&error); g_free(path);
 }
 g_mutex_lock(&pv_gst_lock); pv_gst_current[0] = 0; g_mutex_unlock(&pv_gst_lock);
 const guint loaded = names->len;
 g_ptr_array_unref(names);
 if (!valid) return false;
 if (!loaded) { *reason = g_strdup("no bundled plugins"); return false; }
 /* bundle-runtime.py verify checks the same set at packaging time. */
 const char *required[] = {"whepclientsrc","webrtcbin","nicesrc","nicesink","dtlsdec","srtpdec","vtdec","opusdec",
                           "rtph264depay","rtph265depay","rtpopusdepay","h264parse","h265parse","appsink",NULL};
 const size_t prefix = strlen(pv_gst_plugins);
 for (int i=0; required[i]; i++) {
  GstElementFactory *factory = gst_element_factory_find(required[i]);
  GstPlugin *plugin = factory ? gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory)) : NULL;
  const char *filename = plugin ? gst_plugin_get_filename(plugin) : NULL;
  valid = filename && g_str_has_prefix(filename,pv_gst_plugins) && filename[prefix] == '/';
  if (plugin) gst_object_unref(plugin);
  if (factory) gst_object_unref(factory);
  if (!valid) { *reason = g_strdup_printf("required element %s is missing or not bundled", required[i]); return false; }
 }
 return true;
}

static gpointer pv_gst_loader(gpointer unused)
{
 (void)unused;
 char *reason = NULL;
 const gint64 start = g_get_monotonic_time();
 const bool ready = pv_gst_load(&reason);
 const long long ms = (long long)((g_get_monotonic_time() - start) / 1000);
 if (ready) blog(ms >= PIXELVIEW_GST_SLOW_MS ? LOG_WARNING : LOG_INFO, "[pixelview-whep] GStreamer runtime ready in %lld ms", ms);
 else blog(LOG_ERROR, "[pixelview-whep] GStreamer runtime failed after %lld ms: %s; receiving is unavailable", ms, reason ? reason : "unknown");
 g_free(reason);
 g_mutex_lock(&pv_gst_lock);
 pv_gst_state = ready ? PIXELVIEW_GST_READY : PIXELVIEW_GST_FAILED;
 g_cond_broadcast(&pv_gst_cond);
 g_mutex_unlock(&pv_gst_lock);
 return NULL;
}

/* Any thread: true once the runtime is loaded and checked. */
G_GNUC_UNUSED static bool pixelview_gst_ready(void)
{
 g_mutex_lock(&pv_gst_lock);
 const bool ready = pv_gst_state == PIXELVIEW_GST_READY;
 g_mutex_unlock(&pv_gst_lock);
 return ready;
}

/* Receive worker threads only. Starts the loader once, then waits up to
 * timeout_ms for it. Returns LOADING when the wait ran out or `cancelled`
 * said so, and STALLED instead once the load has run past the overall
 * budget; the load carries on and a later call waits for it again.
 * `cancelled` is called without pv_gst_lock held. */
static enum pixelview_gst_state pixelview_gst_wait(guint timeout_ms, gboolean (*cancelled)(void *), void *data)
{
 g_mutex_lock(&pv_gst_lock);
 if (pv_gst_state == PIXELVIEW_GST_IDLE) {
  pv_gst_state = PIXELVIEW_GST_FAILED;
  if (pv_gst_plugins) {
   GThread *thread = g_thread_try_new("pv-gst-init", pv_gst_loader, NULL, NULL);
   if (thread) { g_thread_unref(thread); pv_gst_state = PIXELVIEW_GST_LOADING; pv_gst_started = g_get_monotonic_time(); }
  }
  if (pv_gst_state == PIXELVIEW_GST_LOADING) blog(LOG_INFO, "[pixelview-whep] loading the bundled GStreamer runtime");
  else blog(LOG_ERROR, "[pixelview-whep] GStreamer runtime could not start; receiving is unavailable");
 }
 const gint64 until = g_get_monotonic_time() + (gint64)timeout_ms * G_TIME_SPAN_MILLISECOND;
 bool warned = false;
 enum pixelview_gst_state state = pv_gst_state;
 while (pv_gst_state == PIXELVIEW_GST_LOADING) {
  g_mutex_unlock(&pv_gst_lock);
  const bool stop = cancelled && cancelled(data);
  g_mutex_lock(&pv_gst_lock);
  state = pv_gst_state;
  if (stop || state != PIXELVIEW_GST_LOADING) break;
  const gint64 now = g_get_monotonic_time();
  const long long loading = (long long)((now - pv_gst_started) / 1000);
  const char *at = pv_gst_current[0] ? pv_gst_current : "startup";
  if (loading >= PIXELVIEW_GST_BUDGET_MS) {
   blog(LOG_ERROR, "[pixelview-whep] GStreamer runtime still loading after %lld ms (at %s); receiving is unavailable until it finishes", loading, at);
   state = PIXELVIEW_GST_STALLED;
   break;
  }
  if (now >= until) {
   blog(LOG_ERROR, "[pixelview-whep] GStreamer runtime still loading after %lld ms (at %s); this receive gives up and the load carries on", loading, at);
   break;
  }
  if (!warned && loading >= PIXELVIEW_GST_SLOW_MS) {
   warned = true;
   blog(LOG_WARNING, "[pixelview-whep] GStreamer runtime still loading after %lld ms (at %s)", loading, at);
  }
  g_cond_wait_until(&pv_gst_cond, &pv_gst_lock, MIN(until, now + 50 * G_TIME_SPAN_MILLISECOND));
 }
 if (state != PIXELVIEW_GST_STALLED) state = pv_gst_state;
 g_mutex_unlock(&pv_gst_lock);
 return state;
}

/* Tests and tools: configure and load synchronously, on the calling thread's
 * behalf (the load itself still runs on the loader thread). */
G_GNUC_UNUSED static bool pixelview_gst_init(void)
{
 return pixelview_gst_configure() && pixelview_gst_wait(60000u, NULL, NULL) == PIXELVIEW_GST_READY;
}
