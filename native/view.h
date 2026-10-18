#pragma once
#include <gtk/gtk.h>
/** Creates an independently owned box, grid, scroller, label, button, entry, checkbox, or select. */
GtkWidget *sg_view_new(int kind);
/** Updates validated text without generating application edit events. */
void sg_view_text(GtkWidget *widget, const char *text);
/** Updates checkbox or selection state without generating application edit events. */
void sg_view_value(GtkWidget *widget, int value);
/** Replaces dropdown options from an owned GTK string list. */
void sg_view_options(GtkWidget *widget, const char *text, const char *key, gboolean clear);
/** Applies layout and common state properties. */
void sg_view_properties(GtkWidget *widget, gboolean enabled, gboolean visible, int margin, int width, int height, int horizontal, int vertical, gboolean expand_x, gboolean expand_y, int spacing);
/** Places a child after a sibling, rejecting cycles and foreign parents. */
gboolean sg_view_place(GtkWidget *parent, GtkWidget *child, GtkWidget *previous, int row, int column, int row_span, int column_span);
/** Detaches a child from supported containers. */
void sg_view_remove(GtkWidget *child);
/** Copies a window reference to its independently owned child. */
gboolean sg_view_window(GtkWindow *window, GtkWidget *child);
/** Borrows the last popped event's captured text until the next pop. */
const char *sg_loop_text(void *loop);
/** Reads the last popped event's captured checkbox or selection value. */
int sg_loop_value(void *loop);
/** Replaces a widget-local CSS provider, rejecting invalid CSS without changing the old provider. */
gboolean sg_view_style(GtkWidget *widget, const char *css);
/** Clears or appends a validated automatic, fixed, or weighted grid track. */
gboolean sg_view_track(GtkWidget *widget, gboolean horizontal, int kind, int value, gboolean clear);
/** Simulates a native text, toggle, or selection edit without notification suppression. */
void sg_view_edit(GtkWidget *widget, const char *text, int value);
/** Reports a widget's allocated dimension for layout verification. */
int sg_view_extent(GtkWidget *widget, gboolean horizontal);
