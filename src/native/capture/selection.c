/** Supplies X11 input and hollow outlines while Sun owns rectangle selection behavior. */
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>

/** Owns an X connection, input grabs, and windows that never cover the selected interior. */
typedef struct {
    Display *display;
    Window bar, outline;
    Cursor cursor;
    int x, y, width, height, event_x, event_y;
    char *error;
} Selection;

/** Opens a bounded screen selector, preserving failure details for the owner. */
void *sg_selection_new(int x, int y, int width, int height) {
    Selection *s = g_new0(Selection, 1);
    s->x = x; s->y = y; s->width = width; s->height = height;
    s->display = XOpenDisplay(NULL);
    if (!s->display) { s->error = g_strdup("Cannot connect to X11 for live area selection"); return s; }
    Display *d = s->display;
    int screen = DefaultScreen(d), event, error;
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
        (gint64)x + width > DisplayWidth(d, screen) || (gint64)y + height > DisplayHeight(d, screen) ||
        !XShapeQueryExtension(d, &event, &error)) {
        s->error = g_strdup("The shared screen cannot be positioned for live selection. Check your display layout."); return s;
    }
    Window root = RootWindow(d, screen);
    s->bar = XCreateSimpleWindow(d, root, x + MAX(0, (width - 480) / 2), y, MIN(width, 480), MIN(height, 36), 0, 0, 0x152135);
    s->outline = XCreateSimpleWindow(d, root, x, y, 1, 1, 0, 0, 0x73e2cb);
    XSetWindowAttributes attributes = {0};
    attributes.override_redirect = True;
    XChangeWindowAttributes(d, s->bar, CWOverrideRedirect, &attributes);
    XChangeWindowAttributes(d, s->outline, CWOverrideRedirect | CWBitGravity, &attributes);
    XShapeCombineRectangles(d, s->outline, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XSelectInput(d, s->bar, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XStoreName(d, s->outline, "Sun capture outline");
    XStoreName(d, s->bar, "Sun capture area selection");
    XMapRaised(d, s->bar);
    XSync(d, False);
    s->cursor = XCreateFontCursor(d, XC_crosshair);
    int pointer = XGrabPointer(d, s->bar, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, s->cursor, CurrentTime);
    int keyboard = XGrabKeyboard(d, s->bar, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (pointer != GrabSuccess || keyboard != GrabSuccess)
        s->error = g_strdup("Cannot grab input for screen selection. Close other dialogs and retry.");
    return s;
}

/** Returns a borrowed selection diagnostic or an empty string on success. */
const char *sg_selection_error(Selection *s) { return s->error ? s->error : ""; }

/** Polls input without blocking; positions are relative to the authorized screen. */
int sg_selection_poll(Selection *s) {
    if (s->error) return -1;
    if (!XPending(s->display)) return 0;
    XEvent event;
    XNextEvent(s->display, &event);
    if (event.type == Expose) {
        GC gc = XCreateGC(s->display, s->bar, 0, NULL);
        XSetForeground(s->display, gc, 0xffffff);
        const char *text = "Drag to select; release to confirm; Esc to cancel";
        XDrawString(s->display, s->bar, gc, 8, 23, text, (int)strlen(text));
        XFreeGC(s->display, gc);
    }
    if (event.type == KeyPress) {
        KeySym key = XLookupKeysym(&event.xkey, 0);
        return key == XK_Escape ? 4 : key == XK_Return ? 5 : 0;
    }
    if (event.type == MotionNotify) {
        s->event_x = event.xmotion.x_root - s->x; s->event_y = event.xmotion.y_root - s->y;
        return 2;
    }
    if ((event.type == ButtonPress || event.type == ButtonRelease) && event.xbutton.button == Button1) {
        s->event_x = event.xbutton.x_root - s->x; s->event_y = event.xbutton.y_root - s->y;
        return event.type == ButtonPress ? 1 : 3;
    }
    return 0;
}

/** Reads the most recently polled pointer coordinate. */
int sg_selection_coordinate(Selection *s, int axis) { return axis ? s->event_y : s->event_x; }

/** Replaces the entire outline shape atomically, or hides an empty rectangle. */
void sg_selection_draw(Selection *s, int x, int y, int width, int height) {
    if (s->error) return;
    if (width <= 0 || height <= 0) { XUnmapWindow(s->display, s->outline); return; }
    if (x < 0 || y < 0 || (gint64)x + width > s->width || (gint64)y + height > s->height) return;
    int thickness = MIN(3, MIN(width, height));
    XRectangle rectangles[] = {{0, 0, width, thickness}, {0, height-thickness, width, thickness},
        {0, 0, thickness, height}, {width-thickness, 0, thickness, height}};
    XGrabServer(s->display);
    XMoveResizeWindow(s->display, s->outline, s->x+x, s->y+y, width, height);
    XShapeCombineRectangles(s->display, s->outline, ShapeBounding, 0, 0, rectangles, 4, ShapeSet, Unsorted);
    XClearWindow(s->display, s->outline); XMapRaised(s->display, s->outline);
    XUngrabServer(s->display); XSync(s->display, False);
}

/** Releases both input grabs and destroys all windows before disconnecting. */
void sg_selection_free(Selection *s) {
    if (!s) return;
    if (s->display) {
        XUngrabPointer(s->display, CurrentTime); XUngrabKeyboard(s->display, CurrentTime);
        if (s->bar) XDestroyWindow(s->display, s->bar);
        if (s->outline) XDestroyWindow(s->display, s->outline);
        if (s->cursor) XFreeCursor(s->display, s->cursor);
        XCloseDisplay(s->display);
    }
    g_free(s->error); g_free(s);
}
