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

/** Checks frame edits and stale timestamps during asynchronous backend seeks. */
int main(void) {
    gtk_init();
    frame_steps();
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
