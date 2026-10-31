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

/** Distinguishes stale timestamps during a gap seek from successful or failed completion. */
int main(void) {
    gtk_init();
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
    g_object_unref(widget);
    return 0;
}
