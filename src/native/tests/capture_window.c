/** Exercises visible-window metadata through the actual native capture pipeline. */
#include "capture/pipeline.c"

/** Adds coordinate-colored pixels and an optional visible window rectangle to synthetic frames. */
static GstPadProbeReturn window_fixture(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    GstVideoInfo video;
    g_assert_true(gst_video_info_from_caps(&video, caps));
    gst_caps_unref(caps);
    GstVideoFrame frame;
    g_assert_true(gst_video_frame_map(&frame, &video, buffer, GST_MAP_WRITE));
    for (int y = 0; y < 240; ++y) {
        guint8 *row = (guint8 *)GST_VIDEO_FRAME_PLANE_DATA(&frame, 0) + y * GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
        for (int x = 0; x < 320; ++x) {
            row[x * 3] = x % 256; row[x * 3 + 1] = y; row[x * 3 + 2] = 80;
        }
    }
    gst_video_frame_unmap(&frame);
    if (mode) {
        GstVideoCropMeta *crop = gst_buffer_add_video_crop_meta(buffer);
        crop->x = 40 + (mode == 4 ? GST_BUFFER_OFFSET(buffer) : 0); crop->y = 30;
        crop->width = mode == 2 ? 400 : (mode == 3 && GST_BUFFER_OFFSET(buffer) > 0 ? 136 : 137); crop->height = 91;
    }
    return GST_PAD_PROBE_OK;
}

/** Checks dimensions and edge pixels, including valid dark content and absent metadata. */
static void capture_window(int mode, gboolean recording) {
    const char *tail = recording ?
        "identity name=screen_geometry ! presenter.sink_0 compositor name=presenter background=black ! capsfilter name=canvas ! video/x-raw,format=RGB ! fakesink name=result sync=false" :
        "fakesink name=result sync=false";
    char *description = g_strdup_printf("videotestsrc num-buffers=3 ! video/x-raw,format=RGB,width=320,height=240,framerate=30/1 ! identity name=fixture ! videocrop name=window_crop ! %s", tail);
    Pipeline *p = sg_pipeline_new(description);
    g_free(description);
    g_assert_null(p->error);
    GstElement *fixture = gst_bin_get_by_name(GST_BIN(p->pipeline), "fixture");
    GstPad *pad = gst_element_get_static_pad(fixture, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, window_fixture, GINT_TO_POINTER(mode), NULL);
    gst_object_unref(pad); gst_object_unref(fixture);
    g_assert_true(sg_pipeline_crop_window(p));
    g_assert_true(sg_pipeline_play(p));
    GstMessage *message = gst_bus_timed_pop_filtered(p->bus, 5 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    g_assert_nonnull(message);
    if (mode == 2 || mode == 3) {
        g_assert_cmpint(GST_MESSAGE_TYPE(message), ==, GST_MESSAGE_ERROR);
    } else {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gst_message_parse_error(message, &error, NULL);
            g_error("Capture failed: %s", error->message);
        }
        GstElement *sink = gst_bin_get_by_name(GST_BIN(p->pipeline), "result");
        GstSample *sample = NULL;
        g_object_get(sink, "last-sample", &sample, NULL);
        g_assert_nonnull(sample);
        GstVideoInfo video;
        g_assert_true(gst_video_info_from_caps(&video, gst_sample_get_caps(sample)));
        int width = mode ? 137 : 320, height = mode ? 91 : 240;
        g_assert_cmpint(GST_VIDEO_INFO_WIDTH(&video), ==, recording ? (width + 1) / 2 * 2 : width);
        g_assert_cmpint(GST_VIDEO_INFO_HEIGHT(&video), ==, recording ? (height + 1) / 2 * 2 : height);
        GstVideoFrame frame;
        g_assert_true(gst_video_frame_map(&frame, &video, gst_sample_get_buffer(sample), GST_MAP_READ));
        guint8 *first = GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);
        guint8 *last = first + (height - 1) * GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0) + (width - 1) * 3;
        g_assert_cmpint(first[0], ==, mode == 4 ? 42 : mode ? 40 : 0);
        g_assert_cmpint(first[1], ==, mode ? 30 : 0);
        g_assert_cmpint(last[0], ==, ((mode == 4 ? 42 : mode ? 40 : 0) + width - 1) % 256);
        g_assert_cmpint(last[1], ==, (mode ? 30 : 0) + height - 1);
        gst_video_frame_unmap(&frame);
        gst_sample_unref(sample); gst_object_unref(sink);
    }
    gst_message_unref(message);
    sg_pipeline_free(p);
}

/** Runs window and full-frame scenarios without a portal or desktop permission. */
int main(void) {
    capture_window(0, FALSE);
    capture_window(1, FALSE);
    capture_window(0, TRUE);
    capture_window(1, TRUE);
    capture_window(2, FALSE);
    capture_window(3, TRUE);
    capture_window(4, TRUE);
    return 0;
}
