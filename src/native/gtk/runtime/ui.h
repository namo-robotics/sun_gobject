#pragma once
#include <gtk/gtk.h>
/** Terminates before GTK access if called outside the initial Linux thread. */
void sg_ui_require_main_thread(void);
/** Checks a length-delimited UTF-8 string and rejects embedded NUL bytes. */
gboolean sg_ui_valid_text(const char *text, gsize length);
/** Owns a button reference and its native clicked-signal subscription. */
typedef struct SgButtonEvents SgButtonEvents;
/** Creates an event subscription that never calls back into Sun. */
SgButtonEvents *sg_button_events_new(GtkButton *button);
/** Disconnects the subscription and releases its button reference. */
void sg_button_events_free(SgButtonEvents *events);
/** Consumes one queued click, returning false when no clicks remain. */
gboolean sg_button_events_take(SgButtonEvents *events);
/** Emits the concrete no-argument clicked signal for automated interaction. */
void sg_button_emit_clicked(GtkButton *button);
/** Reports whether a window is still attached to its application. */
gboolean sg_window_is_open(GtkWindow *window);

/** Stable native queue; it owns its application reference, never Sun state. */
typedef struct SgEventLoop SgEventLoop;
/** One native subscription owned by the Sun callback registry. */
typedef struct SgEventSource SgEventSource;
/** Allocates an empty event queue and retains its application. */
SgEventLoop *sg_loop_new(GtkApplication *application);
/** Retains stable queue storage for a callback context or subscription. */
SgEventLoop *sg_loop_ref(SgEventLoop *loop);
/** Releases queue storage after all owners and subscriptions are gone. */
void sg_loop_unref(SgEventLoop *loop);
/** Returns the globally unique identity of this loop. */
guint64 sg_loop_identity(SgEventLoop *loop);
/** Reserves a never-reused registration number, or zero after completion. */
guint64 sg_loop_reserve(SgEventLoop *loop);
/** Returns the retained application as a borrowed pointer. */
GtkApplication *sg_loop_application(SgEventLoop *loop);
/** Returns whether registration is still permitted. */
gboolean sg_loop_open(SgEventLoop *loop);
/** Enters callback dispatch unless another loop is already running. */
gboolean sg_loop_begin(SgEventLoop *loop);
/** Marks the loop complete, leaves dispatch, and discards queued events. */
void sg_loop_finish(SgEventLoop *loop);
/** Checks for application windows without retaining a temporary list. */
gboolean sg_loop_has_windows(SgEventLoop *loop);
/** Returns queued event count for bounded dispatch. */
guint sg_loop_pending(SgEventLoop *loop);
/** Pops an event ID; with closed_only, discards other event kinds. */
guint64 sg_loop_pop(SgEventLoop *loop, gboolean closed_only);
/** Services the GTK context, blocking only if requested by the Sun dispatcher. */
void sg_loop_iterate(SgEventLoop *loop, gboolean block);
/** Rejects manual iteration while a callback loop owns dispatch. */
void sg_ui_require_manual_iteration(void);
/** Retains an emitter and installs a click or window-removal adapter. */
SgEventSource *sg_loop_connect(SgEventLoop *loop, guint64 id, guint kind, gpointer emitter);
/** Installs a single native timeout; repetition is scheduled by Sun. */
SgEventSource *sg_loop_timer(SgEventLoop *loop, guint64 id, guint delay);
/** Rearms a delivered timer after its Sun callback has completed. */
void sg_loop_rearm(SgEventSource *source, guint delay);
/** Disconnects or destroys a source before releasing its owned references. */
void sg_loop_source_free(SgEventSource *source);
