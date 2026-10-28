/** Connects GTK OpenGL surfaces to independently owned rendering sources. */
#pragma once

#include <gtk/gtk.h>

/** Draws in the current framebuffer using physical pixel dimensions; returns success. */
typedef int (*SgGLRenderCallback)(void *data, int width, int height);

/** Drops context-local resources; GL calls are permitted only when context_available is true. */
typedef void (*SgGLResetCallback)(void *data, int context_available);

/** Acquires or releases one reference to the native rendering source. */
typedef void (*SgGLLifetimeCallback)(void *data);

/** Queues an input record without borrowing application state. */
typedef void (*SgGLInputCallback)(void *data, int kind, int code, double x, double y);

/**
 * Creates an independently referenced GL area and retains its rendering source.
 * All callbacks are required and must remain valid until the source is released.
 * Rendering failures belong to the source's diagnostics; context failures can be
 * read with sg_gl_area_error. Returns null for an incomplete callback contract.
 */
GtkWidget *sg_gl_area_new(void *data, SgGLRenderCallback render, SgGLResetCallback reset,
                          SgGLLifetimeCallback retain, SgGLLifetimeCallback release,
                          SgGLInputCallback input);

/** Requests a redraw without invoking application code immediately. */
void sg_gl_area_queue(GtkWidget *area);

/** Borrows a native context error message, or an empty string when no error exists. */
const char *sg_gl_area_error(GtkWidget *area);

/** Clears GDK's cached current context before another graphics host runs. */
void sg_gl_clear_current(void);
