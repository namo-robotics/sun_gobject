/** Adapts GStreamer streaming-thread caps and bus messages to an owned polling API. */
#include <gst/gst.h>
#include <unistd.h>
#include <fcntl.h>

/** Owns a pipeline and immutable crop parameters used by its streaming callbacks. */
typedef struct {
    GstElement *pipeline;
    GstBus *bus;
    int output_fd, remote_fd;
    char *error;
    int x, y, width, height, logical_width, logical_height;
    int frame_width, frame_height;
} Pipeline;

/** Checks caps before buffers pass downstream; no application callbacks run on this thread. */
static GstPadProbeReturn caps_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    Pipeline *p = data;
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps *caps;
    gst_event_parse_caps(event, &caps);
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    int width = 0, height = 0;
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);
    GstElement *element = gst_pad_get_parent_element(pad);
    if (g_str_equal(GST_OBJECT_NAME(element), "region")) {
        if (width <= 0 || height <= 0 || (p->frame_width &&
            (width != p->frame_width || height != p->frame_height))) {
            GST_ELEMENT_ERROR(element, STREAM, FORMAT, ("The screen size changed during capture. Select the area again."), (NULL));
            gst_object_unref(element);
            return GST_PAD_PROBE_DROP;
        }
        p->frame_width = width; p->frame_height = height;
        int left = (gint64)p->x * width / p->logical_width;
        int top = (gint64)p->y * height / p->logical_height;
        int right = width - ((gint64)(p->x + p->width) * width + p->logical_width - 1) / p->logical_width;
        int bottom = height - ((gint64)(p->y + p->height) * height + p->logical_height - 1) / p->logical_height;
        g_object_set(element, "left", left, "top", top, "right", right, "bottom", bottom, NULL);
    } else {
        gint64 bitrate = CLAMP((gint64)width * height * 30 / 4, 2000000, 120000000);
        g_object_set(element, "target-bitrate", (int)bitrate, NULL);
    }
    gst_object_unref(element);
    return GST_PAD_PROBE_OK;
}

/** Parses a Sun-built pipeline description, retaining parse errors for inspection. */
void *sg_pipeline_new(const char *description) {
    gst_init(NULL, NULL);
    Pipeline *p = g_new0(Pipeline, 1);
    p->output_fd = -1; p->remote_fd = -1;
    GError *error = NULL;
    p->pipeline = gst_parse_launch(description, &error);
    if (error) { p->error = g_strdup(error->message); g_error_free(error); }
    if (p->pipeline && !GST_IS_BIN(p->pipeline)) {
        g_free(p->error); p->error = g_strdup("Capture description must create a pipeline");
    }
    if (p->pipeline) p->bus = gst_element_get_bus(p->pipeline);
    return p;
}

/** Returns the latest borrowed parse or bus diagnostic. */
const char *sg_pipeline_error(Pipeline *p) { return p->error ? p->error : "Cannot start capture pipeline"; }

/** Configures descriptors and source identity while the pipeline is stopped. */
int sg_pipeline_configure(Pipeline *p, int output, int remote, const char *target, const char *node) {
    if (!p->pipeline || p->error) return 0;
    GstElement *sink = gst_bin_get_by_name(GST_BIN(p->pipeline), "output");
    GstElement *source = gst_bin_get_by_name(GST_BIN(p->pipeline), "source");
    if (!sink || !source) { gst_clear_object(&sink); gst_clear_object(&source); return 0; }
    GParamSpec *fd_property = g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "fd");
    GParamSpec *remote_property = g_object_class_find_property(G_OBJECT_GET_CLASS(source), "fd");
    if (!fd_property || G_PARAM_SPEC_VALUE_TYPE(fd_property) != G_TYPE_INT ||
        (remote >= 0 && (!remote_property || G_PARAM_SPEC_VALUE_TYPE(remote_property) != G_TYPE_INT))) {
        gst_object_unref(sink); gst_object_unref(source); return 0;
    }
    p->output_fd = fcntl(output, F_DUPFD_CLOEXEC, 0);
    if (remote >= 0) p->remote_fd = fcntl(remote, F_DUPFD_CLOEXEC, 0);
    if (p->output_fd < 0 || (remote >= 0 && p->remote_fd < 0)) {
        gst_object_unref(sink); gst_object_unref(source); return 0;
    }
    g_object_set(sink, "fd", p->output_fd, NULL);
    if (remote >= 0) {
        g_object_set(source, "fd", p->remote_fd, NULL);
        if (*target && g_object_class_find_property(G_OBJECT_GET_CLASS(source), "target-object"))
            g_object_set(source, "target-object", target, NULL);
        else g_object_set(source, "path", node, NULL);
    }
    gst_object_unref(sink); gst_object_unref(source);
    return 1;
}

/** Validates and stores logical crop bounds before any streaming thread starts. */
int sg_pipeline_crop(Pipeline *p, int x, int y, int width, int height, int logical_width, int logical_height) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || logical_width <= 0 || logical_height <= 0 ||
        (gint64)x + width > logical_width || (gint64)y + height > logical_height) return 0;
    p->x = x; p->y = y; p->width = width; p->height = height;
    p->logical_width = logical_width; p->logical_height = logical_height;
    return 1;
}

/** Installs native caps guards and starts an already configured pipeline. */
int sg_pipeline_play(Pipeline *p) {
    if (!p->pipeline || p->error) return 0;
    const char *names[] = {"region", "encoder"};
    for (int i = 0; i < 2; ++i) {
        GstElement *element = gst_bin_get_by_name(GST_BIN(p->pipeline), names[i]);
        if (!element) continue;
        GstElementFactory *factory = gst_element_get_factory(element);
        const char *factory_name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "";
        if (!g_str_equal(factory_name, i == 0 ? "videocrop" : "vp8enc")) {
            gst_object_unref(element); return 0;
        }
        if (i == 0 && !p->logical_width) { gst_object_unref(element); return 0; }
        GstPad *pad = gst_element_get_static_pad(element, "sink");
        if (!pad) { gst_object_unref(element); return 0; }
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, caps_probe, p, NULL);
        gst_object_unref(pad); gst_object_unref(element);
    }
    return gst_element_set_state(p->pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
}

/** Consumes one bus event: playing, EOS, source failure, other failure, or no relevant message. */
int sg_pipeline_poll(Pipeline *p) {
    if (!p->bus) return -1;
    GstMessage *message = gst_bus_pop(p->bus);
    if (!message) return 0;
    int result = 0;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = NULL;
        gst_message_parse_error(message, &error, NULL);
        g_free(p->error); p->error = g_strdup(error->message); g_error_free(error);
        GstElement *source = gst_bin_get_by_name(GST_BIN(p->pipeline), "source");
        result = GST_MESSAGE_SRC(message) == GST_OBJECT(source) ? 3 : -1;
        gst_clear_object(&source);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        gst_element_set_state(p->pipeline, GST_STATE_NULL);
        result = 2;
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(message) == GST_OBJECT(p->pipeline)) {
        GstState old, current, pending;
        gst_message_parse_state_changed(message, &old, &current, &pending);
        if (current == GST_STATE_PLAYING) result = 1;
    }
    gst_message_unref(message);
    return result;
}

/** Sends EOS downstream of the source so a revoked PipeWire stream can still finalize. */
int sg_pipeline_finish(Pipeline *p) {
    if (!p->pipeline) return 0;
    GstElement *queue = gst_bin_get_by_name(GST_BIN(p->pipeline), "capture_queue");
    if (!queue) return 0;
    GstPad *pad = gst_element_get_static_pad(queue, "sink");
    if (!pad) { gst_object_unref(queue); return 0; }
    int ok = gst_pad_send_event(pad, gst_event_new_eos());
    gst_object_unref(pad); gst_object_unref(queue);
    return ok;
}

/** Stops streaming before releasing callback storage and owned references. */
void sg_pipeline_free(Pipeline *p) {
    if (!p) return;
    if (p->pipeline) gst_element_set_state(p->pipeline, GST_STATE_NULL);
    gst_clear_object(&p->bus); gst_clear_object(&p->pipeline);
    if (p->output_fd >= 0) close(p->output_fd);
    if (p->remote_fd >= 0) close(p->remote_fd);
    g_free(p->error); g_free(p);
}
