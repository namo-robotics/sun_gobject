/** Supplies native test stimuli; Sun owns assertions and lifecycle orchestration. */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>
#include <unistd.h>
#include <fcntl.h>

/** Finds a named test window on a separate X connection. */
static Window find_window(Display *d, const char *wanted) {
    Window root, parent, *children = NULL;
    unsigned int count = 0;
    XQueryTree(d, DefaultRootWindow(d), &root, &parent, &children, &count);
    Window result = None;
    for (unsigned int i = 0; i < count; ++i) {
        char *name = NULL;
        if (XFetchName(d, children[i], &name) && name && g_str_equal(name, wanted)) result = children[i];
        if (name) XFree(name);
    }
    if (children) XFree(children);
    return result;
}

/** Sends a concrete drag or Escape event to the selector's native input window. */
int sg_capture_test_input(int kind, int x, int y) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 0;
    Window window = find_window(d, "Sun capture area selection");
    if (!window) { XCloseDisplay(d); return 0; }
    XEvent e = {0};
    e.xany.display = d; e.xany.window = window;
    long mask;
    if (kind == 2) {
        e.type = MotionNotify; e.xmotion.x_root = x; e.xmotion.y_root = y;
        mask = PointerMotionMask;
    } else if (kind == 4) {
        e.type = KeyPress; e.xkey.keycode = XKeysymToKeycode(d, XK_Escape);
        mask = KeyPressMask;
    } else {
        e.type = kind == 1 ? ButtonPress : ButtonRelease;
        e.xbutton.button = Button1; e.xbutton.x_root = x; e.xbutton.y_root = y;
        mask = kind == 1 ? ButtonPressMask : ButtonReleaseMask;
    }
    int sent = XSendEvent(d, window, False, mask, &e);
    XSync(d, False); XCloseDisplay(d);
    return sent;
}

/** Verifies the server's outline bounds and hollow interior after a resize. */
int sg_capture_test_outline(int x, int y, int width, int height) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 0;
    Window window = find_window(d, "Sun capture outline"), root;
    int px, py, count, ordering;
    unsigned int w, h, border, depth;
    int ok = window && XGetGeometry(d, window, &root, &px, &py, &w, &h, &border, &depth);
    if (ok) ok = px == x && py == y && w == (unsigned)width && h == (unsigned)height;
    if (ok) {
        XRectangle *rectangles = XShapeGetRectangles(d, window, ShapeBounding, &count, &ordering);
        int points[5][2] = {{0,height/2},{width-1,height/2},{width/2,0},{width/2,height-1},{width/2,height/2}};
        for (int j = 0; j < 5; ++j) {
            int inside = 0;
            for (int i = 0; i < count; ++i) {
                XRectangle r = rectangles[i];
                if (points[j][0] >= r.x && points[j][0] < r.x+r.width && points[j][1] >= r.y && points[j][1] < r.y+r.height) inside = 1;
            }
            if (inside != (j < 4)) ok = 0;
        }
        XFree(rectangles);
    }
    XCloseDisplay(d); return ok;
}

/** Owns a mock portal on a private connection and a separate dispatch thread. */
typedef struct {
    GThread *thread;
    GMutex mutex;
    GCond ready;
    GMainContext *context;
    GMainLoop *loop;
    GDBusConnection *bus;
    int started, mode, requests, closed;
} Mock;

/** Handles portal calls, emitting responses before method replies to exercise ordering. */
static void mock_call(GDBusConnection *bus, const char *sender, const char *path,
                      const char *interface, const char *method, GVariant *args,
                      GDBusMethodInvocation *invocation, gpointer data) {
    (void)path; (void)interface;
    Mock *m = data;
    if (g_str_equal(method, "Close")) {
        g_atomic_int_inc(&m->closed);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    if (g_str_equal(method, "OpenPipeWireRemote")) {
        int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        GUnixFDList *fds = g_unix_fd_list_new();
        int index = g_unix_fd_list_append(fds, fd, NULL);
        close(fd);
        g_dbus_method_invocation_return_value_with_unix_fd_list(invocation, g_variant_new("(h)", index), fds);
        g_object_unref(fds); return;
    }
    g_atomic_int_inc(&m->requests);
    GVariant *options = g_variant_get_child_value(args, g_variant_n_children(args)-1);
    const char *token = NULL;
    g_variant_lookup(options, "handle_token", "&s", &token);
    char *unique = g_strdup(sender+1);
    for (char *c = unique; *c; ++c) if (*c == '.') *c = '_';
    char *request = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", unique, token);
    GVariantBuilder result;
    g_variant_builder_init(&result, G_VARIANT_TYPE_VARDICT);
    if (g_str_equal(method, "CreateSession"))
        g_variant_builder_add(&result, "{sv}", "session_handle", g_variant_new_string("/org/freedesktop/portal/desktop/session/test/session"));
    if (g_str_equal(method, "Start") && m->mode != 3) {
        GVariantBuilder props, streams;
        g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
        if (m->mode != 4) {
            g_variant_builder_add(&props, "{sv}", "position", g_variant_new("(ii)", 0, 0));
            g_variant_builder_add(&props, "{sv}", "size", g_variant_new("(ii)", 320, 240));
        }
        g_variant_builder_add(&props, "{sv}", "pipewire-serial", g_variant_new_uint64(123456));
        g_variant_builder_add(&streams, "(ua{sv})", 42, &props);
        g_variant_builder_add(&result, "{sv}", "streams", g_variant_builder_end(&streams));
    }
    guint code = m->mode == 1 ? 1 : m->mode == 2 ? 2 : 0;
    g_dbus_connection_emit_signal(bus, sender, request, "org.freedesktop.portal.Request", "Response",
        g_variant_new("(ua{sv})", code, &result), NULL);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", request));
    g_variant_unref(options); g_free(unique); g_free(request);
}

/** Runs the mock service with thread-local GLib dispatch for synchronous client calls. */
static gpointer mock_thread(gpointer data) {
    Mock *m = data;
    m->context = g_main_context_new();
    g_main_context_push_thread_default(m->context);
    m->loop = g_main_loop_new(m->context, FALSE);
    m->bus = g_dbus_connection_new_for_address_sync(g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, NULL);
    const char *xml = "<node><interface name='org.freedesktop.portal.ScreenCast'>"
        "<method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
        "<method name='SelectSources'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
        "<method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
        "<method name='OpenPipeWireRemote'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='h' direction='out'/></method>"
        "</interface><interface name='org.freedesktop.portal.Session'><method name='Close'/></interface></node>";
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, NULL);
    const GDBusInterfaceVTable table = {.method_call = mock_call};
    guint main_id = g_dbus_connection_register_object(m->bus, "/org/freedesktop/portal/desktop", info->interfaces[0], &table, m, NULL, NULL);
    guint session_id = g_dbus_connection_register_object(m->bus, "/org/freedesktop/portal/desktop/session/test/session", info->interfaces[1], &table, m, NULL, NULL);
    GVariant *reply = g_dbus_connection_call_sync(m->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", "org.freedesktop.portal.Desktop", 0), NULL, 0, 1000, NULL, NULL);
    if (reply) g_variant_unref(reply);
    g_mutex_lock(&m->mutex); m->started = 1; g_cond_signal(&m->ready); g_mutex_unlock(&m->mutex);
    g_main_loop_run(m->loop);
    g_dbus_connection_unregister_object(m->bus, main_id); g_dbus_connection_unregister_object(m->bus, session_id);
    g_dbus_connection_close_sync(m->bus, NULL, NULL); g_object_unref(m->bus);
    g_dbus_node_info_unref(info); g_main_loop_unref(m->loop);
    g_main_context_pop_thread_default(m->context); g_main_context_unref(m->context);
    return NULL;
}

/** Starts an isolated mock with success, cancellation, denial, or malformed-response behavior. */
void *sg_capture_test_portal(int mode) {
    Mock *m = g_new0(Mock, 1); m->mode = mode;
    g_mutex_init(&m->mutex); g_cond_init(&m->ready);
    g_mutex_lock(&m->mutex);
    m->thread = g_thread_new("capture-test-portal", mock_thread, m);
    while (!m->started) g_cond_wait(&m->ready, &m->mutex);
    g_mutex_unlock(&m->mutex); return m;
}

/** Emits permission revocation on the mock session. */
void sg_capture_test_revoke(Mock *m) {
    g_dbus_connection_emit_signal(m->bus, NULL, "/org/freedesktop/portal/desktop/session/test/session",
        "org.freedesktop.portal.Session", "Closed", g_variant_new("(a{sv})", NULL), NULL);
    g_dbus_connection_flush_sync(m->bus, NULL, NULL);
}

/** Returns the number of permission calls or observed session closures. */
int sg_capture_test_count(Mock *m, int closed) { return g_atomic_int_get(closed ? &m->closed : &m->requests); }

/** Stops the mock after all client owners have released their sessions. */
void sg_capture_test_portal_free(Mock *m) {
    g_main_loop_quit(m->loop); g_thread_join(m->thread);
    g_mutex_clear(&m->mutex); g_cond_clear(&m->ready); g_free(m);
}
