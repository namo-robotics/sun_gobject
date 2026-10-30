#pragma once
#include <gtk/gtk.h>

/** Creates an image preview that preserves aspect ratio and reports decoding failures inline. */
GtkWidget *sg_image_new(void);
/** Loads a local image only when its path changes; an empty path clears the preview. */
void sg_image_set(GtkWidget *widget, const char *path);
/** Reports empty, decoded, or failed image state as zero, one, or minus one. */
int sg_image_status(GtkWidget *widget);
