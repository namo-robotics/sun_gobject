#pragma once
#include <gtk/gtk.h>
/** Creates a floating media preview with playback and a draggable time-range selector. */
GtkWidget *sg_media_new(void);
/** Enables range editing or switches to playback and seeking over the full video. */
void sg_media_trim_controls(GtkWidget *widget, int enabled);
/** Sets a rational frame rate; zero numerator retains legacy time-based scrubbing. */
int sg_media_frame_rate(GtkWidget *widget, gint64 numerator, gint64 denominator);
/** Applies a local source and controlled microsecond range without emitting user edits. */
void sg_media_set(GtkWidget *widget, const char *path, gint64 duration, gint64 start, gint64 end, gint64 position);
/** Borrows the owned edit emitter used for queued range-change snapshots. */
GtkWidget *sg_media_events(GtkWidget *widget);
/** Exercises the same gesture callbacks as pointer input using normalized timeline coordinates. */
void sg_media_drag(GtkWidget *widget, double from, double to);
/** Exercises the arrow-key handler; direction must be negative or positive one. */
void sg_media_step(GtkWidget *widget, int direction);
/** Reports error (-1), preparing (0), paused (1), playing (2), or seeking (3). */
int sg_media_status(GtkWidget *widget);
/** Toggles playback of the selected interval through the normal playback handler. */
void sg_media_toggle(GtkWidget *widget);
/** Reports the decoded media timestamp in microseconds for preview verification. */
gint64 sg_media_timestamp(GtkWidget *widget);

/** Atomically replaces ordered id,start,end intervals; rejects malformed or overlapping ranges. */
int sg_media_ranges(GtkWidget *widget, const char *encoded, gint64 active);
