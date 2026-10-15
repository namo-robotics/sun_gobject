#include <glib-object.h>
static int finalized;
/** Records finalization without modifying the object being destroyed. */
static void finalized_notify(gpointer data, GObject *object) {
    (void)data;
    (void)object;
    ++finalized;
}
/** Constructs a floating GInitiallyUnowned object and observes its lifetime. */
gpointer sg_test_floating(void) {
    GObject *object = g_object_new(G_TYPE_INITIALLY_UNOWNED, NULL);
    g_object_weak_ref(object, finalized_notify, NULL);
    return object;
}
/** Reports how many probe objects reached finalization. */
int sg_test_finalized(void) { return finalized; }
/** Allocates an error whose ownership passes to the Sun test. */
GError *sg_test_error(void) { return g_error_new_literal(g_quark_from_static_string("sun-test"), 42, "example error"); }
