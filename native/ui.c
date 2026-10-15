#define _GNU_SOURCE
#include "ui.h"
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

void sg_ui_require_main_thread(void) {
    if (syscall(SYS_gettid) != getpid()) {
        g_error("Sun GTK objects must be used and destroyed on the main thread");
    }
}

gboolean sg_ui_valid_text(const char *text, gsize length) {
    return memchr(text, '\0', length) == NULL && g_utf8_validate(text, length, NULL);
}

/** Keeps signal state independent of Sun stack frames and object moves. */
struct SgButtonEvents {
    GtkButton *button;
    gulong handler;
    guint pending;
};

/** Records a click without invoking user code or overflowing the counter. */
static void clicked(GtkButton *button, gpointer data) {
    (void)button;
    SgButtonEvents *events = data;
    if (events->pending < G_MAXUINT) {
        events->pending++;
    }
}

SgButtonEvents *sg_button_events_new(GtkButton *button) {
    SgButtonEvents *events = g_new0(SgButtonEvents, 1);
    events->button = g_object_ref(button);
    events->handler = g_signal_connect(button, "clicked", G_CALLBACK(clicked), events);
    return events;
}

void sg_button_events_free(SgButtonEvents *events) {
    g_signal_handler_disconnect(events->button, events->handler);
    g_object_unref(events->button);
    g_free(events);
}

gboolean sg_button_events_take(SgButtonEvents *events) {
    if (events->pending == 0) {
        return FALSE;
    }
    events->pending--;
    return TRUE;
}

void sg_button_emit_clicked(GtkButton *button) {
    g_signal_emit_by_name(button, "clicked");
}

gboolean sg_window_is_open(GtkWindow *window) {
    return gtk_window_get_application(window) != NULL;
}

/** A queued delivery contains identifiers, never pointers into Sun storage. */
typedef struct { guint64 id; guint kind; } PendingEvent;
/** Native queue storage survives moves of its Sun owner. */
struct SgEventLoop {
    GtkApplication *application;
    GQueue pending;
    guint refs;
    guint64 identity;
    guint64 next_id;
    gboolean finished;
};
/** Owns the references needed by one signal or timeout adapter. */
struct SgEventSource {
    SgEventLoop *loop;
    guint64 id;
    guint kind;
    GObject *emitter;
    gulong handler;
    GSource *timer;
};
static guint64 next_loop_identity = 1;
static SgEventLoop *active_loop;

/** Adds a delivery without calling user code from a native callback. */
static void enqueue(SgEventSource *source) {
    if (!source->loop->finished) {
        PendingEvent *event = g_new(PendingEvent, 1);
        *event = (PendingEvent){source->id, source->kind};
        g_queue_push_tail(&source->loop->pending, event);
    }
}
/** Converts a clicked signal into a queued registration ID. */
static void queued_click(GtkButton *button, gpointer data) {
    (void)button;
    enqueue(data);
}
/** Observes removal even when retained references delay widget destruction. */
static void window_removed(GtkApplication *application, GtkWindow *window, gpointer data) {
    (void)application;
    SgEventSource *source = data;
    if (G_OBJECT(window) == source->emitter) enqueue(source);
}
/** Queues one timeout and leaves rearming to Sun after callback completion. */
static gboolean timer_ready(gpointer data) {
    SgEventSource *source = data;
    GSource *timer = source->timer;
    source->timer = NULL;
    enqueue(source);
    g_source_unref(timer);
    return G_SOURCE_REMOVE;
}
SgEventLoop *sg_loop_new(GtkApplication *application) {
    sg_ui_require_main_thread();
    if (next_loop_identity == 0) g_error("Event loop identity space exhausted");
    SgEventLoop *loop = g_new0(SgEventLoop, 1);
    loop->application = g_object_ref(application);
    loop->refs = 1;
    loop->identity = next_loop_identity++;
    loop->next_id = 1;
    g_queue_init(&loop->pending);
    return loop;
}
SgEventLoop *sg_loop_ref(SgEventLoop *loop) { loop->refs++; return loop; }
void sg_loop_unref(SgEventLoop *loop) {
    if (--loop->refs == 0) {
        g_queue_clear_full(&loop->pending, g_free);
        g_object_unref(loop->application);
        g_free(loop);
    }
}
guint64 sg_loop_identity(SgEventLoop *loop) { return loop->identity; }
guint64 sg_loop_reserve(SgEventLoop *loop) {
    if (loop->finished || loop->next_id == 0) return 0;
    return loop->next_id++;
}
GtkApplication *sg_loop_application(SgEventLoop *loop) { return loop->application; }
gboolean sg_loop_open(SgEventLoop *loop) { return !loop->finished; }
gboolean sg_loop_begin(SgEventLoop *loop) {
    if (active_loop || loop->finished) return FALSE;
    active_loop = loop;
    return TRUE;
}
void sg_loop_finish(SgEventLoop *loop) {
    loop->finished = TRUE;
    if (active_loop == loop) active_loop = NULL;
    g_queue_clear_full(&loop->pending, g_free);
}
gboolean sg_loop_has_windows(SgEventLoop *loop) {
    return gtk_application_get_windows(loop->application) != NULL;
}
guint sg_loop_pending(SgEventLoop *loop) { return loop->pending.length; }
guint64 sg_loop_pop(SgEventLoop *loop, gboolean closed_only) {
    PendingEvent *event;
    while ((event = g_queue_pop_head(&loop->pending))) {
        guint64 id = event->id;
        guint kind = event->kind;
        g_free(event);
        if (!closed_only || kind == 2) return id;
    }
    return 0;
}
void sg_loop_iterate(SgEventLoop *loop, gboolean block) {
    if (active_loop != loop) g_error("Event loop does not own dispatch");
    g_main_context_iteration(NULL, block);
}
void sg_ui_require_manual_iteration(void) {
    sg_ui_require_main_thread();
    if (active_loop) g_error("Manual GTK iteration during callback dispatch is forbidden");
}
SgEventSource *sg_loop_connect(SgEventLoop *loop, guint64 id, guint kind, gpointer emitter) {
    if (kind == 2 && gtk_window_get_application(GTK_WINDOW(emitter)) != loop->application) return NULL;
    SgEventSource *source = g_new0(SgEventSource, 1);
    source->loop = sg_loop_ref(loop);
    source->id = id;
    source->kind = kind;
    source->emitter = g_object_ref(emitter);
    if (kind == 1) source->handler = g_signal_connect(emitter, "clicked", G_CALLBACK(queued_click), source);
    else source->handler = g_signal_connect(loop->application, "window-removed", G_CALLBACK(window_removed), source);
    return source;
}
SgEventSource *sg_loop_timer(SgEventLoop *loop, guint64 id, guint delay) {
    SgEventSource *source = g_new0(SgEventSource, 1);
    source->loop = sg_loop_ref(loop);
    source->id = id;
    source->kind = 3;
    sg_loop_rearm(source, delay);
    return source;
}
void sg_loop_rearm(SgEventSource *source, guint delay) {
    source->timer = g_timeout_source_new(delay);
    g_source_set_callback(source->timer, timer_ready, source, NULL);
    g_source_attach(source->timer, g_main_context_default());
}
void sg_loop_source_free(SgEventSource *source) {
    if (source->timer) { g_source_destroy(source->timer); g_source_unref(source->timer); }
    if (source->handler) {
        gpointer object = source->kind == 1 ? (gpointer)source->emitter : (gpointer)source->loop->application;
        g_signal_handler_disconnect(object, source->handler);
    }
    if (source->emitter) g_object_unref(source->emitter);
    sg_loop_unref(source->loop);
    g_free(source);
}
