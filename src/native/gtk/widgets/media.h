#pragma once
#include <gtk/gtk.h>
/** Creates a floating media preview with playback and a draggable time-range selector. */
GtkWidget *sg_media_new(void);
/** Applies a local source and controlled microsecond range without emitting user edits. */
void sg_media_set(GtkWidget *widget, const char *path, gint64 duration, gint64 start, gint64 end, gint64 position);
/** Borrows the owned edit emitter used for queued range-change snapshots. */
GtkWidget *sg_media_events(GtkWidget *widget);
/** Exercises the same gesture callbacks as pointer input using normalized timeline coordinates. */
void sg_media_drag(GtkWidget *widget, double from, double to);
/** Reports unavailable, preparing, prepared, or playing media as a checked diagnostic. */
int sg_media_status(GtkWidget *widget);
/** Toggles playback of the selected interval through the normal playback handler. */
void sg_media_toggle(GtkWidget *widget);
/** Reports the decoded media timestamp in microseconds for preview verification. */
gint64 sg_media_timestamp(GtkWidget *widget);
