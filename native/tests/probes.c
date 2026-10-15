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

static int watched_objects;
/** Counts the release of a watched object without retaining it. */
static void watched_finalized(gpointer data, GObject *object) {
    (void)data;
    (void)object;
    --watched_objects;
}
/** Observes a GTK object's lifetime without changing its reference count. */
void sg_test_watch_object(GObject *object) {
    ++watched_objects;
    g_object_weak_ref(object, watched_finalized, NULL);
}
/** Reports watched references still alive in this isolated test process. */
int sg_test_watched_objects(void) { return watched_objects; }

static GObject *held_application;
static guint application_baseline;
/** Keeps a test reference while recording GTK's pre-existing registration references. */
void sg_test_hold_application(GObject *application) {
    application_baseline = application->ref_count;
    held_application = g_object_ref(application);
}
/** Verifies that loop and widget references were released, then releases the test reference. */
int sg_test_application_balanced(void) {
    /* The test reference replaces the destroyed Sun wrapper's reference. GTK
     * may independently retain its registered application after scope exit. */
    int balanced = held_application->ref_count == application_baseline;
    g_object_unref(held_application);
    held_application = NULL;
    return balanced;
}
