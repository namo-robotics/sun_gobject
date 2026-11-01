#include "gtk/widgets/media.h"
#include "gtk/widgets/view.h"
#include "gtk/runtime/ui.h"
#include <math.h>
#include <string.h>
#include <errno.h>

/** One chronological kept interval with an application-owned stable identifier. */
typedef struct { gint64 id, start, end; } MediaRange;

/** Owns native media state; callbacks retain no Sun storage or application references. */
typedef struct {
    GtkWidget *root, *video, *timeline, *transport, *play, *back, *forward, *clock, *notice, *events, *revealer;
    GtkGesture *drag;
    GtkMediaStream *stream;
    GdkSurface *surface;
    gboolean hovered, controls_focused;
    char *path;
    gint64 duration, start, end, position, requested;
    gint64 frame_rate_num, frame_rate_den;
    double origin;
    int handle;
    gboolean trim_controls;
    gboolean stepping;
    GArray *ranges;
    gint64 range_id, emitted_position;
    int boundary;
    gulong notify;
} Media;

/** Selects one kept interval for handle editing without changing playback position. */
static void select_range(Media *media, guint index) {
    MediaRange *range = &g_array_index(media->ranges, MediaRange, index);
    if (media->range_id != range->id) media->handle = 0;
    media->range_id = range->id; media->start = range->start; media->end = range->end;
}
/** Returns the active range index, or no selection for the legacy single interval. */
static int active_index(Media *media) {
    for (guint i = 0; i < media->ranges->len; i++)
        if (g_array_index(media->ranges, MediaRange, i).id == media->range_id) return (int)i;
    return -1;
}
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
/** Keeps paused controls visible and fades them while playing outside the video. */
static void refresh_controls(Media *media) {
    gboolean playing = media->stream && gtk_media_stream_get_playing(media->stream);
    gboolean show = !playing || media->hovered || media->controls_focused;
    gtk_revealer_set_reveal_child(GTK_REVEALER(media->revealer), show);
    gtk_widget_set_can_target(media->revealer, show);
    const char *icon = playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
    if (g_strcmp0(gtk_button_get_icon_name(GTK_BUTTON(media->play)), icon))
        gtk_button_set_icon_name(GTK_BUTTON(media->play), icon);
    const char *label = playing ? "Pause" : (media->trim_controls ? "Play selection" : "Play");
    gtk_widget_set_tooltip_text(media->play, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->play), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
}
/** Reveals playback controls while the pointer is over the video. */
static void video_enter(GtkEventControllerMotion *controller, double x, double y, gpointer data) {
    (void)controller; (void)x; (void)y;
    Media *media = data;
    media->hovered = TRUE;
    refresh_controls(media);
}
/** Fades playing controls when the pointer leaves the video. */
static void video_leave(GtkEventControllerMotion *controller, gpointer data) {
    (void)controller;
    Media *media = data;
    media->hovered = FALSE;
    refresh_controls(media);
}
/** Keeps controls reachable while keyboard focus is inside the overlay. */
static void controls_focus(GObject *object, GParamSpec *property, gpointer data) {
    (void)property;
    Media *media = data;
    media->controls_focused = gtk_event_controller_focus_contains_focus(GTK_EVENT_CONTROLLER_FOCUS(object));
    refresh_controls(media);
}
/** Associates the paintable stream with the picture's native surface. */
static void video_realize(GtkWidget *widget, gpointer data) {
    Media *media = data;
    media->surface = g_object_ref(gtk_native_get_surface(gtk_widget_get_native(widget)));
    if (media->stream) gtk_media_stream_realize(media->stream, media->surface);
}
/** Releases the stream's surface association before the picture is unrealized. */
static void video_unrealize(GtkWidget *widget, gpointer data) {
    (void)widget;
    Media *media = data;
    if (media->stream && media->surface) gtk_media_stream_unrealize(media->stream, media->surface);
    g_clear_object(&media->surface);
}
/** Draws an outlined seek icon without relying on optional desktop icon themes. */
static void draw_jump_icon(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    GdkRGBA color;
    gtk_widget_get_color(GTK_WIDGET(area), &color);
    gdk_cairo_set_source_rgba(cr, &color);
    cairo_scale(cr, width / 32.0, height / 32.0);
    if (GPOINTER_TO_INT(data) < 0) {
        cairo_translate(cr, 32, 0);
        cairo_scale(cr, -1, 1);
    }
    cairo_set_line_width(cr, 2.75);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (int side = 0; side < 2; side++) {
        double x = 4 + side * 13;
        cairo_move_to(cr, x, 6);
        cairo_line_to(cr, x + 11, 16);
        cairo_line_to(cr, x, 26);
        cairo_close_path(cr);
        cairo_stroke(cr);
    }
}
/** Creates a theme-independent icon button matching the outlined play symbol. */
static GtkWidget *jump_button(int direction) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *icon = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(icon), 32);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(icon), 32);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), draw_jump_icon, GINT_TO_POINTER(direction), NULL);
    gtk_button_set_child(GTK_BUTTON(button), icon);
    return button;
}
/** Updates readouts and redraws independently of application rendering. */
static void refresh(Media *media) {
    char *current = time_label(media->position), *end = time_label(media->duration);
    char *label = g_strdup_printf("%s  /  %s", current, end);
    gtk_label_set_text(GTK_LABEL(media->clock), label);
    refresh_controls(media);
    gtk_widget_queue_draw(media->timeline);
    g_free(current); g_free(end); g_free(label);
}
/** Captures a range edit as an owned GTK text value for the existing FIFO event bridge. */
static void emit_edit(Media *media) {
    char *value = g_strdup_printf("%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
                                 media->start, media->end, media->position, media->range_id);
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
/** Advances through kept sections, skips gaps, and reports playhead changes to the application. */
static void stream_changed(GObject *object, GParamSpec *property, gpointer data) {
    (void)object;
    Media *media = data;
    if (gtk_media_stream_get_playing(media->stream)) media->stepping = FALSE;
    const GError *error = gtk_media_stream_get_error(media->stream);
    if (error) {
        gtk_label_set_text(GTK_LABEL(media->notice), "Preview unavailable. Install GTK's GStreamer backend and codecs. Export still works.");
        gtk_widget_set_visible(media->notice, TRUE);
        gtk_widget_set_sensitive(media->play, FALSE);
        gtk_widget_set_sensitive(media->back, FALSE);
        gtk_widget_set_sensitive(media->forward, FALSE);
        return;
    }
    if (!strcmp(property->name, "prepared") && gtk_media_stream_is_prepared(media->stream)) {
        gtk_widget_set_sensitive(media->play, TRUE);
        gtk_widget_set_sensitive(media->back, gtk_media_stream_is_seekable(media->stream));
        gtk_widget_set_sensitive(media->forward, gtk_media_stream_is_seekable(media->stream));
        gtk_widget_set_visible(media->notice, FALSE);
        seek_to(media, media->position);
    }
    if (!strcmp(property->name, "timestamp")) {
        // Keep the requested frame while a paused decoder finishes asynchronous seeks.
        if (!media->stepping) media->position = gtk_media_stream_get_timestamp(media->stream);
        if (gtk_media_stream_get_playing(media->stream)) {
            gint64 next = -1;
            if (media->ranges->len) {
                for (guint i = 0; i < media->ranges->len; i++) {
                    MediaRange range = g_array_index(media->ranges, MediaRange, i);
                    if (media->position < range.end) { next = MAX(media->position, range.start); select_range(media, i); break; }
                }
            } else if (media->position < media->end) next = MAX(media->position, media->start);
            if (next < 0) gtk_media_stream_pause(media->stream);
            else if (next != media->position) seek_to(media, next);
        }
        if (llabs(media->position - media->emitted_position) >= 100000) {
            media->emitted_position = media->position;
            emit_edit(media);
        }
    }
    refresh(media);
}
/** Toggles playback, restarting from the selected in point after reaching either boundary. */
static void toggle_play(GtkButton *button, gpointer data) {
    (void)button;
    Media *media = data;
    if (!media->stream || !gtk_media_stream_is_prepared(media->stream)) return;
    media->stepping = FALSE;
    if (gtk_media_stream_get_playing(media->stream)) gtk_media_stream_pause(media->stream);
    else {
        if (media->ranges->len) {
            gint64 next = g_array_index(media->ranges, MediaRange, 0).start;
            for (guint i = 0; i < media->ranges->len; i++) {
                MediaRange range = g_array_index(media->ranges, MediaRange, i);
                if (media->position < range.end - 1000) { next = MAX(media->position, range.start); break; }
            }
            seek_to(media, next);
        } else if (media->position < media->start || media->position >= media->end - 1000)
            seek_to(media, media->start);
        gtk_media_stream_play(media->stream);
    }
    refresh(media);
}
/** Jumps the playhead by a fixed interval without changing trim bounds or playback state. */
static void jump_time(GtkButton *button, gpointer data) {
    Media *media = data;
    if (!media->stream || !gtk_media_stream_is_prepared(media->stream) ||
        !gtk_media_stream_is_seekable(media->stream) || !gtk_widget_is_sensitive(media->root)) return;
    gint64 delta = GTK_WIDGET(button) == media->back ? -15 * G_USEC_PER_SEC : 15 * G_USEC_PER_SEC;
    media->handle = 0;
    media->boundary = -1;
    media->stepping = !gtk_media_stream_get_playing(media->stream);
    seek_to(media, media->position + delta);
    emit_edit(media);
}
/** Paints the track, excluded regions, selected interval, end handles, ticks, and playhead. */
static void draw_timeline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    (void)area; (void)height;
    Media *media = data;
    cairo_set_source_rgb(cr, .055, .075, .12); cairo_paint(cr);
    cairo_set_source_rgb(cr, .15, .19, .27); cairo_rectangle(cr, 18, 28, width - 36, 44); cairo_fill(cr);
    guint count = MAX(1, media->ranges->len);
    for (guint i = 0; i < count; i++) {
        MediaRange range = media->ranges->len ? g_array_index(media->ranges, MediaRange, i)
                                            : (MediaRange){0, media->start, media->end};
        double first = time_x(media, range.start), last = time_x(media, range.end);
        cairo_set_source_rgb(cr, .14, range.id == media->range_id ? .36 : .25, .45);
        cairo_rectangle(cr, first, 28, MAX(0, last - first), 44); cairo_fill(cr);
    }
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
    for (guint n = 0; media->trim_controls && n < count; n++) {
        MediaRange range = media->ranges->len ? g_array_index(media->ranges, MediaRange, n)
                                            : (MediaRange){0, media->start, media->end};
        for (int side = 0; side < 2; side++) {
            double x = time_x(media, side ? range.end : range.start);
            double direction = side ? -1 : 1;
            if (range.id == media->range_id && media->handle == side + 1)
                cairo_set_source_rgb(cr, 1, .85, .35);
            else cairo_set_source_rgb(cr, .34, .93, .82);
            cairo_rectangle(cr, side ? x - 4 : x, 23, 4, 54); cairo_fill(cr);
            cairo_move_to(cr, x, 40); cairo_line_to(cr, x + direction * 11, 50);
            cairo_line_to(cr, x, 60); cairo_close_path(cr); cairo_fill(cr);
        }
    }
    double playhead = time_x(media, media->position);
    cairo_set_source_rgb(cr, .96, .98, 1); cairo_set_line_width(cr, 2);
    cairo_move_to(cr, playhead, 15); cairo_line_to(cr, playhead, 77); cairo_stroke(cr);
    cairo_move_to(cr, playhead - 5, 12); cairo_line_to(cr, playhead + 5, 12); cairo_line_to(cr, playhead, 19); cairo_close_path(cr); cairo_fill(cr);
}
/** Applies one pointer or keyboard edit with a positive minimum selection length. */
static void edit_at(Media *media, gint64 value, gboolean move_playhead) {
    if (media->duration <= 0 || !gtk_widget_is_sensitive(media->root)) return;
    gint64 previous = media->handle == 1 ? media->start : media->end;
    int index = active_index(media);
    gint64 lower = index > 0 ? g_array_index(media->ranges, MediaRange, index - 1).end : 0;
    gint64 upper = index >= 0 && (guint)(index + 1) < media->ranges->len
        ? g_array_index(media->ranges, MediaRange, index + 1).start : media->duration;
    if (media->handle == 1) {
        gint64 minimum = MIN((gint64)1000, media->end - lower);
        media->start = CLAMP(value, lower, media->end - minimum); value = media->start;
    } else if (media->handle == 2) {
        gint64 minimum = MIN((gint64)1000, upper - media->start);
        media->end = CLAMP(value, media->start + minimum, upper); value = media->end;
    }
    if (index >= 0 && media->handle) {
        MediaRange *range = &g_array_index(media->ranges, MediaRange, index);
        range->start = media->start; range->end = media->end;
    }
    if (media->handle && media->position >= MIN(previous, value) &&
        media->position <= MAX(previous, value)) move_playhead = TRUE;
    if (media->stream) gtk_media_stream_pause(media->stream);
    if (move_playhead) seek_to(media, value);
    emit_edit(media);
}
/** Chooses the nearest end handle or starts playhead scrubbing when the track is pressed. */
static void drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture; (void)y;
    Media *media = data;
    gtk_widget_grab_focus(media->timeline);
    media->origin = x;
    media->boundary = -1;
    double nearest = 17;
    media->handle = 0;
    if (media->trim_controls && media->ranges->len) {
        for (guint i = 0; i < media->ranges->len; i++) {
            MediaRange range = g_array_index(media->ranges, MediaRange, i);
            for (int side = 0; side < 2; side++) {
                double distance = fabs(x - time_x(media, side ? range.end : range.start));
                if (distance < nearest) { nearest = distance; select_range(media, i); media->handle = side + 1; }
            }
            if (i + 1 < media->ranges->len && range.end == g_array_index(media->ranges, MediaRange, i + 1).start
                && fabs(x - time_x(media, range.end)) <= 7) media->boundary = (int)i;
        }
    } else if (media->trim_controls) {
        double left = fabs(x - time_x(media, media->start)), right = fabs(x - time_x(media, media->end));
        media->handle = MIN(left, right) <= 16 ? (left <= right ? 1 : 2) : 0;
    }
    if (!media->handle && media->ranges->len) {
        gint64 position = x_time(media, x);
        for (guint i = 0; i < media->ranges->len; i++) {
            MediaRange range = g_array_index(media->ranges, MediaRange, i);
            if (position >= range.start && position < range.end) { select_range(media, i); break; }
        }
    }
    if (media->boundary >= 0) {
        double boundary = time_x(media, g_array_index(media->ranges, MediaRange, media->boundary).end);
        select_range(media, (guint)(media->boundary + (x > boundary ? 1 : 0)));
        media->handle = x > boundary ? 1 : 2;
    }
    // Selecting a bar must not move it or pull a separate playhead onto it.
    if (!media->handle) {
        media->stepping = FALSE;
        edit_at(media, x_time(media, x), TRUE);
    } else emit_edit(media);
    refresh(media);
}
/** Continues a real drag using offsets from its original timeline coordinate. */
static void drag_update(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture; (void)y;
    Media *media = data;
    if (fabs(x) < 1 && fabs(y) < 1) return;
    if (media->boundary >= 0) {
        if (fabs(x) < 1) return;
        select_range(media, (guint)(media->boundary + (x > 0 ? 1 : 0)));
        media->handle = x > 0 ? 1 : 2;
        media->boundary = -1;
    }
    media->stepping = FALSE;
    edit_at(media, x_time(media, media->origin + x), TRUE);
}
/** Completes a drag using the same clamping rules as intermediate motion. */
static void drag_end(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    drag_update(gesture, x, y, data);
}
/** Finds an adjacent frame boundary without accumulating fractional-rate rounding errors. */
static gint64 adjacent_frame(Media *media, gint64 current, int direction) {
    if (!media->frame_rate_num)
        return CLAMP(current + direction * (gint64)100000, (gint64)0, media->duration);
    long double step = (long double)G_USEC_PER_SEC * media->frame_rate_den / media->frame_rate_num;
    // Decoders may truncate timestamps where the controlled timeline rounds them.
    long double tolerance = MIN(1.L, step / 4);
    long double index = direction > 0 ? floorl((current + tolerance) / step) + 1
                                      : ceill((current - tolerance) / step) - 1;
    gint64 target = (gint64)roundl(MAX(0.L, index) * step);
    // A playhead points at a frame, whereas an out point can end at the duration.
    if (!media->handle && target >= media->duration)
        target = (gint64)roundl(MAX(0.L, ceill((media->duration - .5L) / step) - 1) * step);
    return CLAMP(target, (gint64)0, media->duration);
}
/** Steps the selected bar or playhead, moving them together only when they coincide. */
static gboolean key_pressed(GtkEventControllerKey *controller, guint key, guint code, GdkModifierType modifiers, gpointer data) {
    (void)code; (void)modifiers;
    Media *media = data;
    if (key == GDK_KEY_space) {
        if (controller && !gtk_widget_is_focus(media->root) && !gtk_widget_is_focus(media->timeline))
            return FALSE;
        toggle_play(NULL, media);
        return TRUE;
    }
    if (key != GDK_KEY_Left && key != GDK_KEY_Right) return FALSE;
    if (media->duration <= 0 || !gtk_widget_is_sensitive(media->root)) return FALSE;
    if (controller && !gtk_widget_is_focus(media->timeline)) media->handle = 0;
    gint64 current = media->handle == 1 ? media->start : (media->handle == 2 ? media->end : media->position);
    gboolean together = !media->handle || llabs(media->position - current) <= 1;
    gint64 target = adjacent_frame(media, current, key == GDK_KEY_Left ? -1 : 1);
    media->stepping = TRUE;
    if (media->stream) gtk_media_stream_pause(media->stream);
    if ((media->handle == 1 && target >= media->end) ||
        (media->handle == 2 && target <= media->start)) return TRUE;
    edit_at(media, target, together);
    return TRUE;
}
/** Gives video clicks keyboard focus and selects the playhead without seeking or toggling playback. */
static void video_pressed(GtkGestureClick *gesture, int count, double x, double y, gpointer data) {
    (void)gesture; (void)count; (void)x; (void)y;
    Media *media = data;
    media->handle = 0;
    media->boundary = -1;
    gtk_widget_grab_focus(media->root);
    refresh(media);
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
    g_array_unref(media->ranges);
    g_free(media->path); g_free(media);
}
GtkWidget *sg_media_new(void) {
    sg_ui_require_main_thread();
    Media *media = g_new0(Media, 1);
    media->trim_controls = TRUE;
    media->ranges = g_array_new(FALSE, FALSE, sizeof(MediaRange));
    media->boundary = -1;
    media->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_focusable(media->root, TRUE);
    media->events = g_object_ref_sink(gtk_entry_new());
    media->video = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(media->video), GTK_CONTENT_FIT_CONTAIN);
    sg_view_style_priority(media->video, "* {background-color: black;}", GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_signal_connect(media->video, "realize", G_CALLBACK(video_realize), media);
    g_signal_connect(media->video, "unrealize", G_CALLBACK(video_unrealize), media);
    GtkWidget *viewport = gtk_overlay_new();
    gtk_widget_set_size_request(viewport, 320, 280);
    gtk_widget_set_hexpand(viewport, TRUE);
    gtk_widget_set_vexpand(viewport, TRUE);
    gtk_widget_set_halign(media->video, GTK_ALIGN_FILL);
    gtk_widget_set_valign(media->video, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(viewport), media->video);
    gtk_box_append(GTK_BOX(media->root), viewport);
    GtkGesture *video_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(video_click), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(video_click), GTK_PHASE_CAPTURE);
    g_signal_connect(video_click, "pressed", G_CALLBACK(video_pressed), media);
    gtk_widget_add_controller(viewport, GTK_EVENT_CONTROLLER(video_click));
    media->notice = gtk_label_new("Open a video to preview and trim it here");
    gtk_label_set_wrap(GTK_LABEL(media->notice), TRUE);
    gtk_box_append(GTK_BOX(media->root), media->notice);
    media->transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    media->play = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    media->back = jump_button(-1);
    media->forward = jump_button(1);
    gtk_widget_add_css_class(media->back, "sun-media-transport-button");
    gtk_widget_add_css_class(media->play, "sun-media-transport-button");
    gtk_widget_add_css_class(media->forward, "sun-media-transport-button");
    gtk_widget_set_tooltip_text(media->back, "Jump back 15 seconds");
    gtk_widget_set_tooltip_text(media->forward, "Jump forward 15 seconds");
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->back), GTK_ACCESSIBLE_PROPERTY_LABEL, "Jump back 15 seconds", -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->forward), GTK_ACCESSIBLE_PROPERTY_LABEL, "Jump forward 15 seconds", -1);
    gtk_widget_set_focus_on_click(media->play, FALSE);
    gtk_widget_set_focus_on_click(media->back, FALSE);
    gtk_widget_set_focus_on_click(media->forward, FALSE);
    gtk_widget_set_sensitive(media->play, FALSE);
    gtk_widget_set_sensitive(media->back, FALSE);
    gtk_widget_set_sensitive(media->forward, FALSE);
    media->clock = gtk_label_new("00:00.0 / 00:00.0");
    gtk_box_append(GTK_BOX(media->transport), media->back);
    gtk_box_append(GTK_BOX(media->transport), media->play);
    gtk_box_append(GTK_BOX(media->transport), media->forward);
    gtk_box_append(GTK_BOX(media->root), media->clock);
    sg_view_style_priority(media->transport,
        "* button.sun-media-transport-button { background-image: none; background-color: rgba(32,32,32,0.82); color: white;"
        " border: none; border-radius: 9999px; padding: 16px;"
        " min-width: 32px; min-height: 32px; -gtk-icon-size: 32px; box-shadow: none; }"
        "* button.sun-media-transport-button:hover { background-color: rgba(64,64,64,0.92); }"
        "* button.sun-media-transport-button:active { background-color: rgba(90,90,90,0.95); }"
        "* button.sun-media-transport-button:focus-visible { outline: 2px solid #73e2cb; outline-offset: 2px; }"
        "* button.sun-media-transport-button:disabled { background-color: rgba(32,32,32,0.6); color: #8a8a8a;"
        " border-color: #34475d; opacity: 1; }", GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    media->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(media->revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_revealer_set_transition_duration(GTK_REVEALER(media->revealer), 180);
    gtk_revealer_set_child(GTK_REVEALER(media->revealer), media->transport);
    gtk_widget_set_halign(media->revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(media->revealer, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(viewport), media->revealer);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(video_enter), media);
    g_signal_connect(motion, "leave", G_CALLBACK(video_leave), media);
    gtk_widget_add_controller(viewport, motion);
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "notify::contains-focus", G_CALLBACK(controls_focus), media);
    gtk_widget_add_controller(media->transport, focus);
    media->timeline = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(media->timeline), 108);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(media->timeline), draw_timeline, media, NULL);
    gtk_widget_set_focusable(media->timeline, TRUE);
    gtk_widget_set_cursor_from_name(media->timeline, "ew-resize");
    gtk_widget_set_tooltip_text(media->timeline, "Click a bar to select it; click the track to select the playhead. Arrows: step the selection by one frame; Space: play/pause.");
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->timeline), GTK_ACCESSIBLE_PROPERTY_LABEL, "Video trim timeline", -1);
    gtk_box_append(GTK_BOX(media->root), media->timeline);
    media->drag = gtk_gesture_drag_new();
    g_signal_connect(media->drag, "drag-begin", G_CALLBACK(drag_begin), media);
    g_signal_connect(media->drag, "drag-update", G_CALLBACK(drag_update), media);
    g_signal_connect(media->drag, "drag-end", G_CALLBACK(drag_end), media);
    gtk_widget_add_controller(media->timeline, GTK_EVENT_CONTROLLER(media->drag));
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), media);
    gtk_widget_add_controller(media->root, keys);
    g_signal_connect(media->play, "clicked", G_CALLBACK(toggle_play), media);
    g_signal_connect(media->back, "clicked", G_CALLBACK(jump_time), media);
    g_signal_connect(media->forward, "clicked", G_CALLBACK(jump_time), media);
    g_signal_connect(media->root, "unmap", G_CALLBACK(unmapped), media);
    g_object_set_data_full(G_OBJECT(media->root), "sun-media", media, media_free);
    refresh_controls(media);
    return media->root;
}
void sg_media_trim_controls(GtkWidget *widget, int enabled) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media || media->trim_controls == !!enabled) return;
    media->trim_controls = !!enabled;
    gtk_widget_set_visible(media->timeline, enabled);
    gtk_widget_set_visible(media->transport, TRUE);
    media->handle = 0;
    if (!enabled) { media->start = 0; media->end = media->duration; }
    gtk_widget_set_tooltip_text(media->timeline, enabled
        ? "Click a bar to select it; click the track to select the playhead. Arrows: step the selection by one frame; Space: play/pause."
        : "Click or drag to seek. Arrows: step one frame; Space: play/pause.");
    gtk_accessible_update_property(GTK_ACCESSIBLE(media->timeline), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   enabled ? "Video trim timeline" : "Video playback timeline", -1);
    refresh(media);
}
int sg_media_frame_rate(GtkWidget *widget, gint64 numerator, gint64 denominator) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media || numerator < 0 || numerator > 1000000000 || denominator <= 0 || denominator > 1000000000
        || numerator > denominator * G_USEC_PER_SEC) return 0;
    media->frame_rate_num = numerator;
    media->frame_rate_den = denominator;
    return 1;
}
void sg_media_set(GtkWidget *widget, const char *path, gint64 duration, gint64 start, gint64 end, gint64 position) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media) return;
    gboolean changed = g_strcmp0(media->path, path) != 0;
    media->duration = duration; media->start = media->trim_controls ? start : 0; media->end = media->trim_controls ? end : duration;
    if (changed) {
        media->handle = 0;
        media->stepping = FALSE;
        if (media->stream) {
            g_signal_handler_disconnect(media->stream, media->notify);
            gtk_media_stream_pause(media->stream);
            if (media->surface) gtk_media_stream_unrealize(media->stream, media->surface);
            gtk_picture_set_paintable(GTK_PICTURE(media->video), NULL);
            g_clear_object(&media->stream);
        }
        g_free(media->path); media->path = g_strdup(path);
        media->position = position;
        gtk_widget_set_sensitive(media->play, FALSE);
        gtk_widget_set_sensitive(media->back, FALSE);
        gtk_widget_set_sensitive(media->forward, FALSE);
        gtk_widget_set_visible(media->notice, TRUE);
        gtk_label_set_text(GTK_LABEL(media->notice), *path ? "Loading preview..." : "Open a video to preview and trim it here");
        if (*path) {
            media->stream = GTK_MEDIA_STREAM(gtk_media_file_new_for_filename(path));
            media->notify = g_signal_connect(media->stream, "notify", G_CALLBACK(stream_changed), media);
            if (media->surface) gtk_media_stream_realize(media->stream, media->surface);
            gtk_picture_set_paintable(GTK_PICTURE(media->video), GDK_PAINTABLE(media->stream));
        }
    }
    if (changed || (position != media->requested && position != media->position)) {
        media->stepping = FALSE;
        seek_to(media, position);
    }
    media->requested = position;
    refresh(media);
}
int sg_media_ranges(GtkWidget *widget, const char *encoded, gint64 active) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media) return 0;
    GArray *ranges = g_array_new(FALSE, FALSE, sizeof(MediaRange));
    const char *cursor = encoded;
    gboolean found = !*encoded;
    while (*cursor) {
        MediaRange range;
        gint64 *values[] = {&range.id, &range.start, &range.end};
        for (int i = 0; i < 3; i++) {
            if (!g_ascii_isdigit(*cursor)) goto invalid;
            char *end = NULL;
            errno = 0;
            *values[i] = g_ascii_strtoll(cursor, &end, 10);
            if (errno || end == cursor || (i < 2 && *end != ',')) goto invalid;
            cursor = i < 2 ? end + 1 : end;
        }
        if (ranges->len >= 64 || range.start >= range.end || range.end > media->duration) goto invalid;
        if (ranges->len && range.start < g_array_index(ranges, MediaRange, ranges->len - 1).end) goto invalid;
        for (guint i = 0; i < ranges->len; i++)
            if (g_array_index(ranges, MediaRange, i).id == range.id) goto invalid;
        if (range.id == active) found = TRUE;
        g_array_append_val(ranges, range);
        if (*cursor == ';' && cursor[1]) cursor++;
        else if (*cursor) goto invalid;
    }
    if (!found) goto invalid;
    gboolean changed = ranges->len != media->ranges->len ||
        (ranges->len && memcmp(ranges->data, media->ranges->data, ranges->len * sizeof(MediaRange)) != 0);
    if (changed && media->stream) gtk_media_stream_pause(media->stream);
    g_array_unref(media->ranges); media->ranges = ranges;
    if (media->range_id != active) media->handle = 0;
    media->range_id = active;
    if (ranges->len) select_range(media, (guint)active_index(media));
    refresh(media);
    return 1;
invalid:
    g_array_unref(ranges);
    return 0;
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
void sg_media_step(GtkWidget *widget, int direction) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (media && (direction == -1 || direction == 1))
        key_pressed(NULL, direction < 0 ? GDK_KEY_Left : GDK_KEY_Right, 0, 0, media);
}
int sg_media_status(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    if (!media || !media->stream) return 0;
    if (gtk_media_stream_get_error(media->stream)) return -1;
    if (!gtk_media_stream_is_prepared(media->stream)) return 0;
    if (gtk_media_stream_is_seeking(media->stream)) return 3;
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
