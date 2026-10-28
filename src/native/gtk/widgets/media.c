#include "gtk/widgets/media.h"
#include "gtk/runtime/ui.h"
#include <math.h>
#include <string.h>

/** Owns native media state; callbacks retain no Sun storage or application references. */
typedef struct {
    GtkWidget *root, *video, *timeline, *play, *clock, *notice, *events;
    GtkGesture *drag;
    GtkMediaStream *stream;
    char *path;
    gint64 duration, start, end, position, requested;
    double origin;
    int handle;
    gulong notify;
} Media;

/** Formats a microsecond timestamp for compact playback and timeline labels. */
static char *time_label(gint64 value) {
    gint64 seconds = MAX(0, value) / G_USEC_PER_SEC;
    return g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ".%01" G_GINT64_FORMAT,
                           seconds / 60, seconds % 60, (MAX(0, value) / 100000) % 10);
}
/** Converts a timestamp into an inset timeline coordinate at its current allocation. */
static double time_x(Media *media, gint64 time) {
    double width = MAX(64, gtk_widget_get_width(media->timeline));
    return 18 + (width - 36) * (double)time / MAX((gint64)1, media->duration);
}
/** Maps a pointer position to a clamped timestamp without allowing overflow. */
static gint64 x_time(Media *media, double x) {
    double width = MAX(64, gtk_widget_get_width(media->timeline));
    return (gint64)(CLAMP((x - 18) / (width - 36), 0.0, 1.0) * media->duration);
}
/** Updates readouts and redraws independently of application rendering. */
static void refresh(Media *media) {
    char *current = time_label(media->position), *end = time_label(media->duration);
    char *label = g_strdup_printf("%s  /  %s", current, end);
    gtk_label_set_text(GTK_LABEL(media->clock), label);
    gtk_button_set_label(GTK_BUTTON(media->play), media->stream && gtk_media_stream_get_playing(media->stream) ? "Pause" : "Play selection");
    gtk_widget_queue_draw(media->timeline);
    g_free(current); g_free(end); g_free(label);
}
/** Captures a range edit as an owned GTK text value for the existing FIFO event bridge. */
static void emit_edit(Media *media) {
    char *value = g_strdup_printf("%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
                                 media->start, media->end, media->position);
    gtk_editable_set_text(GTK_EDITABLE(media->events), value);
    g_free(value);
    refresh(media);
}
/** Seeks only prepared streams while retaining the desired timestamp during loading. */
static void seek_to(Media *media, gint64 position) {
    media->position = CLAMP(position, (gint64)0, media->duration);
    if (media->stream && gtk_media_stream_is_prepared(media->stream) && gtk_media_stream_is_seekable(media->stream))
        gtk_media_stream_seek(media->stream, media->position);
    refresh(media);
}
/** Updates native playback state and stops at the selected out point. */
static void stream_changed(GObject *object, GParamSpec *property, gpointer data) {
    (void)object;
    Media *media = data;
    const GError *error = gtk_media_stream_get_error(media->stream);
    if (error) {
        gtk_label_set_text(GTK_LABEL(media->notice), "Preview unavailable. Install GTK's GStreamer backend and codecs. Export still works.");
        gtk_widget_set_visible(media->notice, TRUE);
        gtk_widget_set_sensitive(media->play, FALSE);
        return;
    }
    if (!strcmp(property->name, "prepared") && gtk_media_stream_is_prepared(media->stream)) {
        gtk_widget_set_sensitive(media->play, TRUE);
        gtk_widget_set_visible(media->notice, FALSE);
        seek_to(media, media->position);
    }
    if (!strcmp(property->name, "timestamp")) {
        media->position = gtk_media_stream_get_timestamp(media->stream);
        if (media->position >= media->end && gtk_media_stream_get_playing(media->stream))
            gtk_media_stream_pause(media->stream);
    }
    refresh(media);
}
/** Toggles playback, restarting from the selected in point after reaching either boundary. */
static void toggle_play(GtkButton *button, gpointer data) {
    (void)button;
    Media *media = data;
    if (!media->stream || !gtk_media_stream_is_prepared(media->stream)) return;
    if (gtk_media_stream_get_playing(media->stream)) gtk_media_stream_pause(media->stream);
    else {
        if (media->position < media->start || media->position >= media->end - 1000)
            seek_to(media, media->start);
        gtk_media_stream_play(media->stream);
    }
    refresh(media);
}
/** Paints the track, excluded regions, selected interval, end handles, ticks, and playhead. */
static void draw_timeline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    (void)area; (void)height;
    Media *media = data;
    cairo_set_source_rgb(cr, .055, .075, .12); cairo_paint(cr);
    double left = time_x(media, media->start), right = time_x(media, media->end);
    cairo_set_source_rgb(cr, .15, .19, .27); cairo_rectangle(cr, 18, 28, width - 36, 44); cairo_fill(cr);
    cairo_set_source_rgb(cr, .14, .31, .45); cairo_rectangle(cr, left, 28, MAX(0, right - left), 44); cairo_fill(cr);
    cairo_set_line_width(cr, 1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    for (int i = 0; i <= 4; i++) {
        double x = 18 + (width - 36) * i / 4.0;
        cairo_set_source_rgb(cr, .55, .62, .72); cairo_move_to(cr, x, 78); cairo_line_to(cr, x, 83); cairo_stroke(cr);
        char *text = time_label(media->duration * i / 4);
        cairo_text_extents_t extents; cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, CLAMP(x - extents.width / 2, 4, width - extents.width - 4), 99); cairo_show_text(cr, text); g_free(text);
    }
    for (int i = 0; i < 2; i++) {
        double x = i == 0 ? left : right;
        cairo_set_source_rgb(cr, .34, .93, .82); cairo_rectangle(cr, x - 7, 23, 14, 54); cairo_fill(cr);
        cairo_set_source_rgb(cr, .035, .12, .14); cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x - 2, 41); cairo_line_to(cr, x - 2, 59);
        cairo_move_to(cr, x + 2, 41); cairo_line_to(cr, x + 2, 59); cairo_stroke(cr);
    }
    double playhead = time_x(media, media->position);
    cairo_set_source_rgb(cr, .96, .98, 1); cairo_set_line_width(cr, 2);
    cairo_move_to(cr, playhead, 15); cairo_line_to(cr, playhead, 77); cairo_stroke(cr);
    cairo_move_to(cr, playhead - 5, 12); cairo_line_to(cr, playhead + 5, 12); cairo_line_to(cr, playhead, 19); cairo_close_path(cr); cairo_fill(cr);
}
/** Applies one pointer or keyboard edit with a positive minimum selection length. */
static void edit_at(Media *media, gint64 value) {
    if (media->duration <= 0 || !gtk_widget_is_sensitive(media->root)) return;
    if (media->handle == 1) {
        gint64 minimum = MIN((gint64)100000, media->end);
        media->start = CLAMP(value, (gint64)0, media->end - minimum); value = media->start;
    } else if (media->handle == 2) {
        gint64 minimum = MIN((gint64)100000, media->duration - media->start);
        media->end = CLAMP(value, media->start + minimum, media->duration); value = media->end;
    }
    if (media->stream) gtk_media_stream_pause(media->stream);
    seek_to(media, value);
    emit_edit(media);
}
/** Chooses the nearest end handle or starts playhead scrubbing when the track is pressed. */
static void drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture; (void)y;
    Media *media = data;
    gtk_widget_grab_focus(media->timeline);
    media->origin = x;
    double left = fabs(x - time_x(media, media->start)), right = fabs(x - time_x(media, media->end));
    media->handle = MIN(left, right) <= 16 ? (left <= right ? 1 : 2) : 0;
    edit_at(media, x_time(media, x));
}
/** Continues a real drag using offsets from its original timeline coordinate. */
static void drag_update(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture; (void)y;
    Media *media = data;
    edit_at(media, x_time(media, media->origin + x));
}
/** Completes a drag using the same clamping rules as intermediate motion. */
static void drag_end(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    drag_update(gesture, x, y, data);
    ((Media *)data)->handle = 0;
}
/** Supports arrow-key scrubbing, modified end-handle edits, and spacebar playback. */
static gboolean key_pressed(GtkEventControllerKey *controller, guint key, guint code, GdkModifierType modifiers, gpointer data) {
    (void)controller; (void)code;
    Media *media = data;
    if (key == GDK_KEY_space) { toggle_play(NULL, media); return TRUE; }
    if (key != GDK_KEY_Left && key != GDK_KEY_Right) return FALSE;
    media->handle = modifiers & GDK_SHIFT_MASK ? 1 : (modifiers & GDK_CONTROL_MASK ? 2 : 0);
    gint64 current = media->handle == 1 ? media->start : (media->handle == 2 ? media->end : media->position);
    edit_at(media, current + (key == GDK_KEY_Left ? -100000 : 100000));
    media->handle = 0;
    return TRUE;
}
/** Pauses playback when the owning preview leaves the visible widget tree. */
static void unmapped(GtkWidget *widget, gpointer data) {
    (void)widget;
    Media *media = data;
    if (media->stream) gtk_media_stream_pause(media->stream);
}
/** Disconnects media notifications before releasing stream and event-emitter ownership. */
static void media_free(gpointer data) {
    Media *media = data;
    if (media->stream) {
        g_signal_handler_disconnect(media->stream, media->notify);
        gtk_media_stream_pause(media->stream);
        g_object_unref(media->stream);
    }
    g_object_unref(media->events);
    g_free(media->path); g_free(media);
}
GtkWidget *sg_media_new(void) {
    sg_ui_require_main_thread();
    Media *media = g_new0(Media, 1);
    media->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    media->events = g_object_ref_sink(gtk_entry_new());
    media->video = gtk_video_new();
    gtk_video_set_autoplay(GTK_VIDEO(media->video), FALSE);
    gtk_video_set_graphics_offload(GTK_VIDEO(media->video), GTK_GRAPHICS_OFFLOAD_DISABLED);
    GtkWidget *viewport = gtk_overlay_new();
    gtk_widget_set_size_request(viewport, 320, 280);
    gtk_widget_set_hexpand(viewport, TRUE);
    gtk_widget_set_vexpand(viewport, TRUE);
    gtk_widget_set_halign(media->video, GTK_ALIGN_FILL);
    gtk_widget_set_valign(media->video, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(viewport), media->video);
    gtk_box_append(GTK_BOX(media->root), viewport);
    media->notice = gtk_label_new("Open a video to preview and trim it here");
    gtk_label_set_wrap(GTK_LABEL(media->notice), TRUE);
    gtk_box_append(GTK_BOX(media->root), media->notice);
    GtkWidget *transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    media->play = gtk_button_new_with_label("Play selection");
    gtk_widget_set_sensitive(media->play, FALSE);
    media->clock = gtk_label_new("00:00.0 / 00:00.0");
    gtk_box_append(GTK_BOX(transport), media->play); gtk_box_append(GTK_BOX(transport), media->clock);
    gtk_box_append(GTK_BOX(media->root), transport);
    media->timeline = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(media->timeline), 108);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(media->timeline), draw_timeline, media, NULL);
    gtk_widget_set_focusable(media->timeline, TRUE);
    gtk_widget_set_cursor_from_name(media->timeline, "ew-resize");
    gtk_widget_set_tooltip_text(media->timeline, "Drag the mint handles to trim. Click to seek. Arrows: scrub; Shift+arrows: start; Ctrl+arrows: end; Space: play/pause.");
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->timeline), GTK_ACCESSIBLE_PROPERTY_LABEL, "Video trim timeline", -1);
    gtk_box_append(GTK_BOX(media->root), media->timeline);
    media->drag = gtk_gesture_drag_new();
    g_signal_connect(media->drag, "drag-begin", G_CALLBACK(drag_begin), media);
    g_signal_connect(media->drag, "drag-update", G_CALLBACK(drag_update), media);
    g_signal_connect(media->drag, "drag-end", G_CALLBACK(drag_end), media);
    gtk_widget_add_controller(media->timeline, GTK_EVENT_CONTROLLER(media->drag));
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), media);
    gtk_widget_add_controller(media->timeline, keys);
    g_signal_connect(media->play, "clicked", G_CALLBACK(toggle_play), media);
    g_signal_connect(media->root, "unmap", G_CALLBACK(unmapped), media);
    g_object_set_data_full(G_OBJECT(media->root), "sun-media", media, media_free);
    return media->root;
}
void sg_media_set(GtkWidget *widget, const char *path, gint64 duration, gint64 start, gint64 end, gint64 position) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media) return;
    gboolean changed = g_strcmp0(media->path, path) != 0;
    media->duration = duration; media->start = start; media->end = end;
    if (changed) {
        if (media->stream) {
            g_signal_handler_disconnect(media->stream, media->notify);
            gtk_media_stream_pause(media->stream);
            gtk_video_set_media_stream(GTK_VIDEO(media->video), NULL);
            g_clear_object(&media->stream);
        }
        g_free(media->path); media->path = g_strdup(path);
        media->position = position;
        gtk_widget_set_sensitive(media->play, FALSE);
        gtk_widget_set_visible(media->notice, TRUE);
        gtk_label_set_text(GTK_LABEL(media->notice), *path ? "Loading preview..." : "Open a video to preview and trim it here");
        if (*path) {
            media->stream = GTK_MEDIA_STREAM(gtk_media_file_new_for_filename(path));
            media->notify = g_signal_connect(media->stream, "notify", G_CALLBACK(stream_changed), media);
            gtk_video_set_media_stream(GTK_VIDEO(media->video), media->stream);
        }
    }
    if (changed || position != media->requested) seek_to(media, position);
    media->requested = position;
    refresh(media);
}
GtkWidget *sg_media_events(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    return media ? media->events : NULL;
}
void sg_media_drag(GtkWidget *widget, double from, double to) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media) return;
    double first = time_x(media, (gint64)(from * media->duration));
    double last = time_x(media, (gint64)(to * media->duration));
    g_signal_emit_by_name(media->drag, "drag-begin", first, 50.0);
    g_signal_emit_by_name(media->drag, "drag-update", last - first, 0.0);
    g_signal_emit_by_name(media->drag, "drag-end", last - first, 0.0);
}
int sg_media_status(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media || !media->stream) return 0;
    if (gtk_media_stream_get_error(media->stream)) return -1;
    if (!gtk_media_stream_is_prepared(media->stream)) return 0;
    return gtk_media_stream_get_playing(media->stream) ? 2 : 1;
}
void sg_media_toggle(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (media) toggle_play(NULL, media);
}
gint64 sg_media_timestamp(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    return media && media->stream ? gtk_media_stream_get_timestamp(media->stream) : 0;
}
