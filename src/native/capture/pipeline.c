/** Adapts GStreamer streaming-thread caps and bus messages to an owned polling API. */
#include <gst/gst.h>
#include <gst/video/video.h>
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
    GstCaps *window_caps;
    GstEvent *window_segment;
    gboolean forwarding_window_events, window_started, crop_window;
    GMutex layout_lock;
    int screen_width, screen_height, camera_width, camera_height, corner, size;
} Pipeline;

/** Applies the visible rectangle before downstream caps fix the recording canvas. */
static GstPadProbeReturn window_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    Pipeline *p = data;
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        if (p->forwarding_window_events) return GST_PAD_PROBE_OK;
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            gst_caps_replace(&p->window_caps, caps);
            p->window_started = FALSE;
            return GST_PAD_PROBE_DROP;
        }
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT && !p->window_started) {
            gst_event_replace(&p->window_segment, event);
            return GST_PAD_PROBE_DROP;
        }
        return GST_PAD_PROBE_OK;
    }
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    GstVideoCropMeta *crop = gst_buffer_get_video_crop_meta(buffer);
    int width = 0, height = 0;
    if (p->window_caps) {
        const GstStructure *caps = gst_caps_get_structure(p->window_caps, 0);
        gst_structure_get_int(caps, "width", &width);
        gst_structure_get_int(caps, "height", &height);
    }
    guint x = crop ? crop->x : 0, y = crop ? crop->y : 0;
    guint w = crop ? crop->width : (guint)MAX(width, 0);
    guint h = crop ? crop->height : (guint)MAX(height, 0);
    GstElement *element = gst_bin_get_by_name(GST_BIN(p->pipeline), "window_crop");
    if (width <= 0 || height <= 0 || !w || !h ||
        (guint64)x + w > (guint)width || (guint64)y + h > (guint)height) {
        GST_ELEMENT_ERROR(element, STREAM, FORMAT, ("Invalid visible window rectangle"), (NULL));
        gst_object_unref(element);
        return GST_PAD_PROBE_DROP;
    }
    g_object_set(element, "left", (int)x, "top", (int)y,
        "right", width - (int)(x + w), "bottom", height - (int)(y + h), NULL);
    gst_object_unref(element);
    // Consume the metadata so videocrop does not add its offsets a second time.
    if (crop) {
        buffer = gst_buffer_make_writable(buffer);
        GST_PAD_PROBE_INFO_DATA(info) = buffer;
        crop = gst_buffer_get_video_crop_meta(buffer);
        gst_buffer_remove_meta(buffer, &crop->meta);
    }
    if (!p->window_started) {
        p->forwarding_window_events = TRUE;
        gboolean ok = gst_pad_push_event(pad, gst_event_new_caps(p->window_caps));
        if (ok && p->window_segment) {
            GstEvent *segment = p->window_segment;
            p->window_segment = NULL;
            ok = gst_pad_push_event(pad, segment);
        }
        p->forwarding_window_events = FALSE;
        if (!ok) return GST_PAD_PROBE_DROP;
        p->window_started = TRUE;
    }
    return GST_PAD_PROBE_OK;
}

/** Fixes the output canvas and camera rectangle from negotiated source dimensions. */
static GstPadProbeReturn layout_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    Pipeline *p = data;
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps *caps;
    gst_event_parse_caps(event, &caps);
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    int width = 0, height = 0;
    gst_structure_get_int(s, "width", &width); gst_structure_get_int(s, "height", &height);
    GstElement *element = gst_pad_get_parent_element(pad);
    gboolean screen = g_str_equal(GST_OBJECT_NAME(element), "screen_geometry");
    g_mutex_lock(&p->layout_lock);
    int old_width = screen ? p->screen_width : p->camera_width;
    int old_height = screen ? p->screen_height : p->camera_height;
    if (width <= 0 || height <= 0 || (old_width && (old_width != width || old_height != height))) {
        g_mutex_unlock(&p->layout_lock);
        GST_ELEMENT_ERROR(element, STREAM, FORMAT, ("Capture source dimensions changed during recording"), (NULL));
        gst_object_unref(element); return GST_PAD_PROBE_DROP;
    }
    if (screen) { p->screen_width = width; p->screen_height = height; }
    else { p->camera_width = width; p->camera_height = height; }
    int sw = p->screen_width, sh = p->screen_height, cw = p->camera_width, ch = p->camera_height;
    g_mutex_unlock(&p->layout_lock);
    gst_object_unref(element);
    GstElement *mixer = gst_bin_get_by_name(GST_BIN(p->pipeline), "presenter");
    if (screen && mixer) {
        GstElement *canvas = gst_bin_get_by_name(GST_BIN(p->pipeline), "canvas");
        if (canvas) {
            GstCaps *output = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, (sw + 1) / 2 * 2,
                "height", G_TYPE_INT, (sh + 1) / 2 * 2, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
            g_object_set(canvas, "caps", output, NULL); gst_caps_unref(output); gst_object_unref(canvas);
        }
    }
    if (mixer && sw && sh && cw && ch) {
        GstPad *inset = gst_element_get_static_pad(mixer, "sink_1");
        if (inset) {
            double fraction = p->size == 0 ? .2 : p->size == 1 ? .3 : .4;
            double scale = MIN(sw * fraction / cw, sh * fraction / ch);
            int w = MAX(1, (int)(cw * scale)), h = MAX(1, (int)(ch * scale));
            g_object_set(inset, "width", w, "height", h, "xpos", (p->corner == 0 || p->corner == 2) ? sw - w : 0,
                "ypos", p->corner < 2 ? sh - h : 0, NULL);
            gst_object_unref(inset);
        }
    }
    gst_clear_object(&mixer);
    return GST_PAD_PROBE_OK;
}

/** Stores a validated camera layout before streaming begins. */
int sg_pipeline_presenter(Pipeline *p, int corner, int size) {
    if (!p || corner < 0 || corner > 3 || size < 0 || size > 2) return 0;
    p->corner = corner; p->size = size; return 1;
}

/** Enables visible-window metadata handling before the pipeline starts. */
int sg_pipeline_crop_window(Pipeline *p) {
    if (!p || !p->pipeline) return 0;
    GstElement *crop = gst_bin_get_by_name(GST_BIN(p->pipeline), "window_crop");
    if (!crop) return 0;
    GstElementFactory *factory = gst_element_get_factory(crop);
    int valid = factory && g_str_equal(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "videocrop");
    gst_object_unref(crop);
    p->crop_window = valid;
    return valid;
}

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
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "target-bitrate"))
            g_object_set(element, "target-bitrate", (int)bitrate, NULL);
        else g_object_set(element, "bitrate", (guint)(bitrate / 1000), NULL);
    }
    gst_object_unref(element);
    return GST_PAD_PROBE_OK;
}

/** Parses a Sun-built pipeline description, retaining parse errors for inspection. */
void *sg_pipeline_new(const char *description) {
    gst_init(NULL, NULL);
    Pipeline *p = g_new0(Pipeline, 1);
    g_mutex_init(&p->layout_lock);
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
    GstElement *window = p->crop_window ? gst_bin_get_by_name(GST_BIN(p->pipeline), "window_crop") : NULL;
    if (window) {
        GstElementFactory *factory = gst_element_get_factory(window);
        if (!factory || !g_str_equal(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "videocrop")) {
            gst_object_unref(window); return 0;
        }
        GstPad *sink = gst_element_get_static_pad(window, "sink");
        GstPad *pad = gst_pad_get_peer(sink);
        gst_object_unref(sink);
        if (!pad) { gst_object_unref(window); return 0; }
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER,
            window_probe, p, NULL);
        gst_object_unref(pad); gst_object_unref(window);
    }
    const char *names[] = {"region", "encoder"};
    for (int i = 0; i < 2; ++i) {
        GstElement *element = gst_bin_get_by_name(GST_BIN(p->pipeline), names[i]);
        if (!element) continue;
        GstElementFactory *factory = gst_element_get_factory(element);
        const char *factory_name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "";
        if ((i == 0 && !g_str_equal(factory_name, "videocrop")) ||
            (i == 1 && !g_str_equal(factory_name, "vp8enc") && !g_str_equal(factory_name, "x264enc"))) {
            gst_object_unref(element); return 0;
        }
        if (i == 0 && !p->logical_width) { gst_object_unref(element); return 0; }
        GstPad *pad = gst_element_get_static_pad(element, "sink");
        if (!pad) { gst_object_unref(element); return 0; }
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, caps_probe, p, NULL);
        gst_object_unref(pad); gst_object_unref(element);
    }
    const char *geometry[] = {"screen_geometry", "camera_geometry"};
    for (int i = 0; i < 2; ++i) {
        GstElement *element = gst_bin_get_by_name(GST_BIN(p->pipeline), geometry[i]);
        if (!element) continue;
        GstPad *pad = gst_element_get_static_pad(element, "sink");
        if (!pad) { gst_object_unref(element); return 0; }
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, layout_probe, p, NULL);
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
    int ok = 1;
    const char *names[] = {"capture_queue", "camera_queue", "microphone_queue"};
    for (int i = 0; i < 3; ++i) {
        GstElement *queue = gst_bin_get_by_name(GST_BIN(p->pipeline), names[i]);
        if (!queue) { if (i == 0) ok = 0; continue; }
        GstPad *pad = gst_element_get_static_pad(queue, "sink");
        if (!pad || !gst_pad_send_event(pad, gst_event_new_eos())) ok = 0;
        gst_clear_object(&pad); gst_object_unref(queue);
    }
    return ok;
}

/** Stops streaming before releasing callback storage and owned references. */
void sg_pipeline_free(Pipeline *p) {
    if (!p) return;
    if (p->pipeline) gst_element_set_state(p->pipeline, GST_STATE_NULL);
    gst_clear_object(&p->bus); gst_clear_object(&p->pipeline);
    if (p->output_fd >= 0) close(p->output_fd);
    if (p->remote_fd >= 0) close(p->remote_fd);
    gst_clear_caps(&p->window_caps);
    gst_clear_event(&p->window_segment);
    g_mutex_clear(&p->layout_lock);
    g_free(p->error); g_free(p);
}
