/** Includes the widget implementation to inject a delayed backend into its private state. */
#include "gtk/widgets/media.c"

/** Retains a seek request until the test explicitly completes it. */
typedef struct { GtkMediaStream parent; gint64 target; } DelayedStream;
/** Supplies the minimal playback and seek operations for the test stream. */
typedef struct { GtkMediaStreamClass parent; } DelayedStreamClass;
/** Registers the controlled asynchronous media backend. */
GType delayed_stream_get_type(void);
G_DEFINE_TYPE(DelayedStream, delayed_stream, GTK_TYPE_MEDIA_STREAM)

/** Accepts playback without starting a real decoder. */
static gboolean delayed_play(GtkMediaStream *stream) { (void)stream; return TRUE; }
/** Pauses the synthetic backend without additional work. */
static void delayed_pause(GtkMediaStream *stream) { (void)stream; }
/** Records the target while leaving GTK's timestamp unchanged during the seek. */
static void delayed_seek(GtkMediaStream *stream, gint64 target) {
    ((DelayedStream *)stream)->target = target;
}
/** Installs the controlled playback operations. */
static void delayed_stream_class_init(DelayedStreamClass *klass) {
    GtkMediaStreamClass *stream = GTK_MEDIA_STREAM_CLASS(klass);
    stream->play = delayed_play;
    stream->pause = delayed_pause;
    stream->seek = delayed_seek;
}
/** Initializes the recorded seek target. */
static void delayed_stream_init(DelayedStream *stream) { stream->target = -1; }

/** Exercises frame stepping, persistent bar selection, coupling, and bounded edits. */
static void frame_steps(void) {
    GtkWidget *widget = g_object_ref_sink(sg_media_new());
    sg_media_set(widget, "", 4000000, 0, 4000000, 0);
    g_assert_true(sg_media_frame_rate(widget, 24, 1));
    g_assert_false(sg_media_frame_rate(widget, 24, 0));
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    for (int i = 0; i < 24; i++) key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 1000000);
    for (int i = 0; i < 24; i++) key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    g_assert_cmpint(media->position, ==, 0);
    g_assert_true(sg_media_frame_rate(widget, 30000, 1001));
    for (int i = 0; i < 30; i++) key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 1001000);
    key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 1001000);

    g_assert_true(sg_media_frame_rate(widget, 24, 1));
    media->position = 50000;
    key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    g_assert_cmpint(media->position, ==, 41667);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 83333);
    media->position = 41666;
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 83333);
    gtk_widget_allocate(media->timeline, 1000, 108, -1, NULL);
    sg_media_drag(widget, 0, 0);
    g_assert_cmpint(media->handle, ==, 1);
    g_assert_cmpint(media->position, ==, 83333);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->start, ==, 41667);
    g_assert_cmpint(media->position, ==, 83333);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->start, ==, 125000);
    g_assert_cmpint(media->position, ==, 125000);
    g_assert_cmpint(media->handle, ==, 1);
    sg_media_drag(widget, .5, .5);
    g_assert_cmpint(media->handle, ==, 0);
    g_assert_cmpint(media->position, ==, 2000000);
    sg_media_drag(widget, 1, 1);
    key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    g_assert_cmpint(media->end, ==, 3958333);
    g_assert_cmpint(media->position, ==, 2000000);
    media->position = media->end;
    key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    g_assert_cmpint(media->end, ==, 3916667);
    g_assert_cmpint(media->position, ==, media->end);

    g_assert_true(sg_media_ranges(widget, "1,0,1000000;2,2000000,4000000", 1));
    media->handle = 2;
    for (int i = 0; i < 30; i++) key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->end, ==, 2000000);
    g_assert_true(sg_media_ranges(widget, "1,0,2000000;2,2000000,4000000", 2));
    g_assert_cmpint(media->handle, ==, 0);
    drag_begin(NULL, time_x(media, 2000000) + 2, 50, media);
    drag_end(NULL, 0, 0, media);
    g_assert_cmpint(media->range_id, ==, 2);
    g_assert_cmpint(media->handle, ==, 1);
    drag_begin(NULL, time_x(media, 2000000) - 2, 50, media);
    drag_end(NULL, 0, 0, media);
    g_assert_cmpint(media->range_id, ==, 1);
    g_assert_cmpint(media->handle, ==, 2);
    select_range(media, 1);
    media->handle = 1;
    key_pressed(NULL, GDK_KEY_Left, 0, 0, media);
    g_assert_cmpint(media->start, ==, 2000000);
    media->handle = 0;
    media->position = 3958333;
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 3958333);
    media->start = 3958333;
    media->end = 4000000;
    media->handle = 1;
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->start, ==, 3958333);
    gtk_widget_set_sensitive(widget, FALSE);
    g_assert_false(key_pressed(NULL, GDK_KEY_Left, 0, 0, media));
    g_object_unref(widget);
}

/** Picks up an off-frame playhead when either trim bar crosses it in either direction. */
static void crossing_playhead(void) {
    GtkWidget *widget = g_object_ref_sink(sg_media_new());
    sg_media_set(widget, "", 4000000, 0, 4000000, 0);
    g_assert_true(sg_media_frame_rate(widget, 24, 1));
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    for (int handle = 1; handle <= 2; handle++) {
        for (int direction = -1; direction <= 1; direction += 2) {
            media->start = handle == 1 ? 1000000 : 0;
            media->end = handle == 2 ? 1000000 : 4000000;
            media->handle = handle;
            media->position = 1000000 + direction * 60000;
            guint key = direction > 0 ? GDK_KEY_Right : GDK_KEY_Left;
            key_pressed(NULL, key, 0, 0, media);
            g_assert_cmpint(media->position, ==, 1000000 + direction * 60000);
            key_pressed(NULL, key, 0, 0, media);
            g_assert_cmpint(media->position, ==, handle == 1 ? media->start : media->end);
            key_pressed(NULL, key, 0, 0, media);
            g_assert_cmpint(media->position, ==, 1000000 + direction * 125000);
        }
    }
    // A neighbouring section stops the bar before it can reach the playhead.
    g_assert_true(sg_media_ranges(widget, "1,0,1000000;2,1020000,4000000", 1));
    media->handle = 2;
    media->position = 1030000;
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->end, ==, 1020000);
    g_assert_cmpint(media->position, ==, 1030000);
    g_object_unref(widget);
}

/** Returns an owned controller of the requested type to exercise its installed signals. */
static GtkEventController *find_controller(GtkWidget *widget, GType type) {
    GListModel *controllers = gtk_widget_observe_controllers(widget);
    for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
        GtkEventController *controller = g_list_model_get_item(controllers, i);
        if (G_TYPE_CHECK_INSTANCE_TYPE(controller, type)) {
            g_object_unref(controllers);
            return controller;
        }
        g_object_unref(controller);
    }
    g_object_unref(controllers);
    g_assert_not_reached();
}

/** Checks video focus, parent key routing, and frame stepping with the timeline hidden. */
static void video_keyboard(void) {
    GtkWidget *window = gtk_window_new();
    GtkWidget *widget = sg_media_new();
    gtk_window_set_child(GTK_WINDOW(window), widget);
    sg_media_set(widget, "", 4000000, 0, 4000000, 1000000);
    g_assert_true(sg_media_frame_rate(widget, 24, 1));
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    GtkEventController *keys = find_controller(widget, GTK_TYPE_EVENT_CONTROLLER_KEY);
    g_assert_cmpint(gtk_event_controller_get_propagation_phase(keys), ==, GTK_PHASE_CAPTURE);
    GtkWidget *viewport = gtk_widget_get_parent(media->video);
    GtkEventController *click = find_controller(viewport, GTK_TYPE_GESTURE_CLICK);
    media->handle = 1;
    g_signal_emit_by_name(click, "pressed", 1, 100., 100.);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(window)) == widget);
    g_assert_cmpint(media->handle, ==, 0);
    gboolean handled = FALSE;
    g_signal_emit_by_name(keys, "key-pressed", GDK_KEY_Right, 0, 0, &handled);
    g_assert_true(handled);
    g_assert_cmpint(media->position, ==, 1041667);
    g_assert_cmpint(media->start, ==, 0);
    gtk_widget_grab_focus(media->timeline);
    media->handle = 1;
    g_signal_emit_by_name(keys, "key-pressed", GDK_KEY_Right, 0, 0, &handled);
    g_assert_cmpint(media->start, ==, 41667);
    g_assert_cmpint(media->position, ==, 1041667);
    sg_media_trim_controls(widget, FALSE);
    g_signal_emit_by_name(click, "pressed", 1, 100., 100.);
    g_signal_emit_by_name(keys, "key-pressed", GDK_KEY_Left, 0, 0, &handled);
    g_assert_cmpint(media->position, ==, 1000000);
    g_object_unref(click);
    g_object_unref(keys);
    gtk_window_destroy(GTK_WINDOW(window));
}

/** Exercises transport jumps while paused and playing, including both source boundaries. */
static void transport_jumps(void) {
    GtkWidget *widget = g_object_ref_sink(sg_media_new());
    sg_media_set(widget, "", 60000000, 1000000, 59000000, 20000000);
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    g_assert_false(gtk_widget_get_sensitive(media->back));
    g_assert_false(gtk_widget_get_sensitive(media->forward));
    DelayedStream *backend = g_object_new(delayed_stream_get_type(), NULL);
    media->stream = GTK_MEDIA_STREAM(backend);
    g_free(media->path);
    media->path = g_strdup("synthetic-video");
    media->notify = g_signal_connect(media->stream, "notify", G_CALLBACK(stream_changed), media);
    gtk_media_stream_stream_prepared(media->stream, TRUE, TRUE, TRUE, 60000000);
    g_assert_true(gtk_widget_get_sensitive(media->back));
    g_assert_true(gtk_widget_get_sensitive(media->forward));
    media->handle = 1;
    g_signal_emit_by_name(media->back, "clicked");
    g_assert_cmpint(media->position, ==, 5000000);
    g_assert_cmpint(backend->target, ==, 5000000);
    g_assert_cmpint(media->handle, ==, 0);
    g_assert_false(gtk_media_stream_get_playing(media->stream));
    g_signal_emit_by_name(media->back, "clicked");
    g_assert_cmpint(media->position, ==, 0);
    g_signal_emit_by_name(media->forward, "clicked");
    g_assert_cmpint(media->position, ==, 15000000);
    gtk_media_stream_play(media->stream);
    g_assert_cmpstr(gtk_button_get_icon_name(GTK_BUTTON(media->play)), ==, "media-playback-pause-symbolic");
    g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(media->revealer)));
    video_enter(NULL, 0, 0, media);
    g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(media->revealer)));
    video_leave(NULL, media);
    g_assert_false(gtk_revealer_get_reveal_child(GTK_REVEALER(media->revealer)));
    g_signal_emit_by_name(media->forward, "clicked");
    g_assert_cmpint(media->position, ==, 30000000);
    g_assert_true(gtk_media_stream_get_playing(media->stream));
    media->position = 55000000;
    g_signal_emit_by_name(media->forward, "clicked");
    g_assert_cmpint(media->position, ==, 60000000);
    g_assert_cmpint(media->start, ==, 1000000);
    g_assert_cmpint(media->end, ==, 59000000);
    gtk_media_stream_pause(media->stream);
    g_assert_true(gtk_revealer_get_reveal_child(GTK_REVEALER(media->revealer)));
    g_assert_cmpstr(gtk_button_get_icon_name(GTK_BUTTON(media->play)), ==, "media-playback-start-symbolic");
    sg_media_trim_controls(widget, FALSE);
    g_assert_true(gtk_widget_get_visible(media->transport));
    sg_media_set(widget, "", 0, 0, 0, 0);
    g_assert_false(gtk_widget_get_sensitive(media->back));
    g_assert_false(gtk_widget_get_sensitive(media->forward));
    g_object_unref(widget);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
/** Keeps circular icon controls styled above the editor theme and centered over the video. */
static void transport_style(void) {
    GtkWidget *widget = g_object_ref_sink(sg_media_new());
    g_assert_true(sg_view_style(widget,
        "* button {border:none;outline:none;box-shadow:none;transition:none;"
        "background-image:none;background-color:inherit;color:inherit;}"));
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    g_assert_true(GTK_IS_PICTURE(media->video));
    g_assert_cmpint(gtk_widget_get_halign(media->revealer), ==, GTK_ALIGN_CENTER);
    g_assert_cmpint(gtk_widget_get_valign(media->revealer), ==, GTK_ALIGN_CENTER);
    g_assert_true(gtk_widget_get_parent(media->video) == gtk_widget_get_parent(media->revealer));
    g_assert_true(gtk_widget_get_parent(media->clock) == media->root);
    g_assert_true(GTK_IS_DRAWING_AREA(gtk_button_get_child(GTK_BUTTON(media->back))));
    g_assert_true(GTK_IS_DRAWING_AREA(gtk_button_get_child(GTK_BUTTON(media->forward))));
    GtkWidget *buttons[] = {media->back, media->play, media->forward};
    for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) {
        GtkStyleContext *context = gtk_widget_get_style_context(buttons[i]);
        GtkBorder border, padding;
        gtk_style_context_get_border(context, &border);
        gtk_style_context_get_padding(context, &padding);
        g_assert_cmpint(border.left, ==, 0);
        g_assert_cmpint(border.top, ==, 0);
        g_assert_cmpint(padding.left, ==, 16);
        g_assert_cmpint(padding.top, ==, 16);
    }
    g_object_unref(widget);
}
G_GNUC_END_IGNORE_DEPRECATIONS

/** Checks frame edits, transport controls, styling, and asynchronous backend seeks. */
int main(void) {
    gtk_init();
    frame_steps();
    crossing_playhead();
    video_keyboard();
    transport_jumps();
    transport_style();
    GtkWidget *widget = g_object_ref_sink(sg_media_new());
    sg_media_set(widget, "", 4000000, 0, 1500000, 0);
    g_assert_true(sg_media_ranges(widget, "1,0,1500000;2,2500000,4000000", 1));
    Media *media = g_object_get_data(G_OBJECT(widget), "sun-media");
    DelayedStream *backend = g_object_new(delayed_stream_get_type(), NULL);
    media->stream = GTK_MEDIA_STREAM(backend);
    gtk_media_stream_stream_prepared(media->stream, TRUE, TRUE, TRUE, 4000000);
    media->notify = g_signal_connect(media->stream, "notify", G_CALLBACK(stream_changed), media);
    gtk_media_stream_play(media->stream);
    gtk_media_stream_update(media->stream, 1800000);
    g_assert_cmpint(backend->target, ==, 2500000);
    g_assert_true(gtk_media_stream_is_seeking(media->stream));
    g_assert_cmpint(sg_media_timestamp(widget), ==, 1800000);
    g_assert_cmpint(sg_media_status(widget), ==, 3);
    gtk_media_stream_update(media->stream, 2500000);
    gtk_media_stream_seek_success(media->stream);
    g_assert_cmpint(sg_media_status(widget), ==, 2);
    g_assert_cmpint(sg_media_timestamp(widget), ==, 2500000);
    gtk_media_stream_update(media->stream, 1800000);
    gtk_media_stream_seek_failed(media->stream);
    g_assert_cmpint(sg_media_status(widget), ==, 2);
    g_assert_cmpint(sg_media_timestamp(widget), ==, 1800000);
    media->position = 1800000;
    g_assert_true(sg_media_frame_rate(widget, 24, 1));
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_false(gtk_media_stream_get_playing(media->stream));
    g_assert_cmpint(media->position, ==, 1833333);
    gtk_media_stream_update(media->stream, 1800000);
    key_pressed(NULL, GDK_KEY_Right, 0, 0, media);
    g_assert_cmpint(media->position, ==, 1875000);
    g_object_unref(widget);
    return 0;
}
