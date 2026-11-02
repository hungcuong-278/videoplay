#include <gst/gst.h>
#include <cstdio>
#include <atomic>
#include <chrono>
// Accurate seek on a paused playbin; count buffers leaving the decoder.
static std::atomic<int> decoded(0);
static GstPadProbeReturn onOut(GstPad *, GstPadProbeInfo *, gpointer) { ++decoded; return GST_PAD_PROBE_OK; }
static void onAdded(GstBin *, GstBin *, GstElement *e, gpointer) {
    if (g_str_has_prefix(GST_ELEMENT_NAME(e), "avdec")) { GstPad *p = gst_element_get_static_pad(e, "sink");
        gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER, onOut, nullptr, nullptr); gst_object_unref(p); } }
int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    GstElement *p = gst_element_factory_make("playbin", nullptr);
    g_signal_connect(p, "deep-element-added", G_CALLBACK(onAdded), nullptr);
    g_object_set(p, "uri", gst_filename_to_uri(argv[1], nullptr), "video-sink", gst_element_factory_make("fakesink", nullptr), nullptr);
    gst_element_set_state(p, GST_STATE_PAUSED); gst_element_get_state(p, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    long long targets[] = {600000, 1500000, 2850000, 3300000, 45000};
    int total = 0;
    for (long long t : targets) {
        decoded = 0; auto t0 = std::chrono::steady_clock::now();
        gst_element_seek_simple(p, GST_FORMAT_TIME, (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), t * GST_MSECOND);
        gst_element_get_state(p, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("  target %7lld ms: %4d frames fed to decoder, %5.0f ms\n", t, decoded.load(), ms); total += decoded;
    }
    printf("  average %d frames fed to decoder per accurate seek (one GOP = 250 frames here)\n", total / 5);
    gst_element_set_state(p, GST_STATE_NULL); return 0;
}
