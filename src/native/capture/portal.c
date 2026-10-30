/** Bridges portal callbacks into polled state owned by one Sun wrapper. */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <unistd.h>

/** Owns a bus connection, subscriptions, results, and an authorized remote descriptor. */
typedef struct {
    GDBusConnection *bus;
    char *session, *request, *error;
    GVariant *results, *properties;
    guint response_subscription, closed_subscription;
    int result, closed, fd, step;
    guint32 node;
} Portal;

/** Replaces the diagnostic with an owned native error message. */
static void fail(Portal *p, GError *error) {
    g_free(p->error);
    p->error = g_strdup(error ? error->message : "Invalid desktop portal response");
    g_clear_error(&error);
    p->result = -1;
}

/** Records revocation without invoking Sun from a native callback. */
static void closed(GDBusConnection *bus, const char *sender, const char *path,
                   const char *interface, const char *signal, GVariant *parameters, gpointer data) {
    (void)bus; (void)sender; (void)path; (void)interface; (void)signal; (void)parameters;
    Portal *p = data;
    if (p->session && g_str_equal(path, p->session)) p->closed = 1;
}

/** Retains the response dictionary until the next explicit request. */
static void response(GDBusConnection *bus, const char *sender, const char *path,
                     const char *interface, const char *signal, GVariant *parameters, gpointer data) {
    (void)sender; (void)path; (void)interface; (void)signal;
    Portal *p = data;
    guint32 code;
    g_variant_get(parameters, "(u@a{sv})", &code, &p->results);
    g_dbus_connection_signal_unsubscribe(bus, p->response_subscription);
    p->response_subscription = 0;
    g_clear_pointer(&p->request, g_free);
    p->result = code == 0 ? 1 : code == 1 ? 2 : -1;
    if (code > 1) fail(p, NULL);
    const char *session = NULL;
    if (code == 0 && g_variant_lookup(p->results, "session_handle", "&s", &session)) {
        p->session = g_strdup(session);

    }
    GVariant *streams = g_variant_lookup_value(p->results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
    if (code == 0 && ((p->step == 0 && !p->session) || (p->step == 2 && !streams))) fail(p, NULL);
    if (streams) {
        if (g_variant_n_children(streams) != 1) fail(p, NULL);
        else g_variant_get_child(streams, 0, "(u@a{sv})", &p->node, &p->properties);
        g_variant_unref(streams);
    }
}

/** Allocates an idle connection owner, preserving connection errors for the caller. */
void *sg_portal_new(void) {
    Portal *p = g_new0(Portal, 1);
    p->fd = -1; p->step = -1;
    GError *error = NULL;
    p->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!p->bus) fail(p, error);
    else p->closed_subscription = g_dbus_connection_signal_subscribe(p->bus,
        "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Session", "Closed",
        NULL, NULL, 0, closed, p, NULL);
    return p;
}

/** Returns a borrowed diagnostic valid until the next operation or destruction. */
const char *sg_portal_error(Portal *p) { return p->error ? p->error : "Desktop portal failed"; }

/** Sends one explicitly selected ScreenCast method after subscribing to its response path. */
int sg_portal_request(Portal *p, int step, int types, int cursor) {
    if (!p->bus || p->response_subscription || step < 0 || step > 2 || step != p->step + 1 || (step && !p->session)) return 0;
    g_clear_pointer(&p->results, g_variant_unref);
    p->result = 0;
    p->step = step;
    char *uuid = g_uuid_string_random();
    for (char *c = uuid; *c; ++c) if (*c == '-') *c = '_';
    char *token = g_strconcat("sun_", uuid, NULL);
    char *sender = g_strdup(g_dbus_connection_get_unique_name(p->bus) + 1);
    for (char *c = sender; *c; ++c) if (*c == '.') *c = '_';
    p->request = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    p->response_subscription = g_dbus_connection_signal_subscribe(p->bus,
        "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response",
        p->request, NULL, 0, response, p, NULL);
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
    if (step == 0) g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(token));
    if (step == 1) {
        g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(types));
        g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
        g_variant_builder_add(&options, "{sv}", "cursor_mode", g_variant_new_uint32(cursor));
    }
    GVariant *args = step == 0 ? g_variant_new("(a{sv})", &options) :
        step == 1 ? g_variant_new("(oa{sv})", p->session, &options) :
                    g_variant_new("(osa{sv})", p->session, "", &options);
    const char *method = step == 0 ? "CreateSession" : step == 1 ? "SelectSources" : "Start";
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(p->bus, "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast", method,
        args, G_VARIANT_TYPE("(o)"), 0, 10000, NULL, &error);
    g_free(uuid); g_free(token); g_free(sender);
    if (!reply) { fail(p, error); return 0; }
    const char *returned;
    g_variant_get(reply, "(&o)", &returned);
    gboolean matches = g_str_equal(returned, p->request);
    g_variant_unref(reply);
    if (!matches) { fail(p, NULL); return 0; }
    return 1;
}

/** Pumps pending GLib events and reports response, denial, failure, or session closure. */
int sg_portal_poll(Portal *p) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    if (p->closed) return 3;
    int result = p->result;
    if (result > 0) p->result = 0;
    return result;
}

/** Reads portal screen geometry; a missing field returns the caller-visible sentinel. */
int sg_portal_geometry(Portal *p, int field) {
    int x, y;
    if (!p->properties || !g_variant_lookup(p->properties, field < 2 ? "position" : "size", "(ii)", &x, &y)) return -1;
    return field % 2 ? y : x;
}

/** Opens and owns the authorized PipeWire descriptor without starting a stream. */
int sg_portal_remote(Portal *p) {
    if (!p->session || p->closed) return -1;
    if (p->fd >= 0) return p->fd;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GUnixFDList *fds = NULL;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(p->bus,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast", "OpenPipeWireRemote",
        g_variant_new("(oa{sv})", p->session, &options), G_VARIANT_TYPE("(h)"),
        0, 10000, NULL, &fds, NULL, &error);
    if (!reply) { fail(p, error); return -1; }
    int index;
    g_variant_get(reply, "(h)", &index);
    p->fd = fds ? g_unix_fd_list_get(fds, index, &error) : -1;
    g_variant_unref(reply);
    g_clear_object(&fds);
    if (p->fd < 0) fail(p, error);
    return p->fd;
}

/** Copies the preferred PipeWire target identifier into an owned string. */
char *sg_portal_target(Portal *p, int serial) {
    guint64 value;
    if (serial && p->properties && g_variant_lookup(p->properties, "pipewire-serial", "t", &value))
        return g_strdup_printf("%" G_GUINT64_FORMAT, value);
    return serial ? g_strdup("") : g_strdup_printf("%u", p->node);
}

/** Cancels outstanding requests, closes the session, and releases every owned resource. */
void sg_portal_free(Portal *p) {
    if (!p) return;
    if (p->response_subscription) g_dbus_connection_signal_unsubscribe(p->bus, p->response_subscription);
    if (p->closed_subscription) g_dbus_connection_signal_unsubscribe(p->bus, p->closed_subscription);
    const char *paths[] = {p->request, p->closed ? NULL : p->session};
    const char *interfaces[] = {"org.freedesktop.portal.Request", "org.freedesktop.portal.Session"};
    for (int i = 0; i < 2; ++i) if (paths[i]) {
        GVariant *reply = g_dbus_connection_call_sync(p->bus, "org.freedesktop.portal.Desktop",
            paths[i], interfaces[i], "Close", NULL, NULL, 0, 1000, NULL, NULL);
        if (reply) g_variant_unref(reply);
    }
    if (p->fd >= 0) close(p->fd);
    g_clear_object(&p->bus);
    g_clear_pointer(&p->results, g_variant_unref);
    g_clear_pointer(&p->properties, g_variant_unref);
    g_free(p->session); g_free(p->request); g_free(p->error); g_free(p);
}
