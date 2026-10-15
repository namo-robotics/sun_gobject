#include "signals.h"
#include <stddef.h>
/* Sun cannot express GValue's union. The support layer reserves two ABI slots;
 * these assertions are checked against the exact headers we link against. */
_Static_assert(sizeof(void *) == 8, "only x86_64 is supported");
_Static_assert(sizeof(GValue) == 24, "GValue size changed");
_Static_assert(_Alignof(GValue) == 8, "GValue alignment changed");
_Static_assert(offsetof(GValue, g_type) == 0, "GValue type offset changed");
_Static_assert(offsetof(GValue, data) == 8, "GValue data offset changed");
_Static_assert(sizeof(GError) == 16, "GError size changed");
_Static_assert(offsetof(GError, message) == 8, "GError message offset changed");
/** Stores the callback pair until the signal closure is released. */
typedef struct {
    SgVoidCallback callback;
    gpointer userdata;
    SgDestroyCallback destroy;
} Connection;
/** Calls a void callback without retaining the closure across user code. */
static void invoke(gpointer instance, gpointer data) {
    Connection *connection = data;
    /* Copy before calling: the callback may disconnect and free connection. */
    SgVoidCallback callback = connection->callback;
    gpointer userdata = connection->userdata;
    callback(instance, userdata);
}
/** Adapts the notify signal while discarding its property descriptor. */
static void notify(gpointer instance, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    invoke(instance, data);
}
/** Releases userdata and its closure allocation exactly once. */
static void destroy(gpointer data, GClosure *closure) {
    (void)closure;
    Connection *connection = data;
    connection->destroy(connection->userdata);
    g_free(connection);
}
/** Validates the signal ABI before creating a GClosure connection. */
static gulong connect(gpointer object, const char *signal, SgVoidCallback callback,
                      gpointer userdata, SgDestroyCallback cleanup, gboolean notification) {
    guint id = 0;
    GQuark detail = 0;
    GSignalQuery query = {0};
    /* Never cast a callback until the runtime signature is validated. */
    if (!g_signal_parse_name(signal, G_OBJECT_TYPE(object), &id, &detail, TRUE)) {
        cleanup(userdata);
        return 0;
    }
    g_signal_query(id, &query);
    if (query.return_type != G_TYPE_NONE ||
        (!notification && query.n_params != 0) ||
        (notification && (query.n_params != 1 ||
          (query.param_types[0] & ~G_SIGNAL_TYPE_STATIC_SCOPE) != G_TYPE_PARAM))) {
        cleanup(userdata);
        return 0;
    }
    Connection *connection = g_new(Connection, 1);
    *connection = (Connection){callback, userdata, cleanup};
    return g_signal_connect_data(object, signal,
        notification ? G_CALLBACK(notify) : G_CALLBACK(invoke), connection, destroy, 0);
}
gulong sg_connect_void(gpointer object, const char *signal, SgVoidCallback callback,
                       gpointer userdata, SgDestroyCallback cleanup) {
    return connect(object, signal, callback, userdata, cleanup, FALSE);
}
gulong sg_connect_notify(gpointer object, const char *signal, SgVoidCallback callback,
                         gpointer userdata, SgDestroyCallback cleanup) {
    return connect(object, signal, callback, userdata, cleanup, TRUE);
}
