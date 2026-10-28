/** Integrates an owned rendering source with GTK's context and input lifetimes. */
#include "gtk/surfaces/gl_area.h"
#include "gtk/runtime/ui.h"

/** Identifies input records in the rendering-source callback protocol. */
typedef enum {
    SOURCE_INPUT_MOTION = 1,
    SOURCE_INPUT_BUTTON = 2,
    SOURCE_INPUT_KEY = 3,
    SOURCE_INPUT_SCROLL = 4,
    SOURCE_INPUT_FOCUS = 5
} SourceInputKind;

/** Retains a native callback target independently of any Sun stack frame. */
typedef struct {
    void *data;
    SgGLRenderCallback render;
    SgGLResetCallback reset;
    SgGLLifetimeCallback release;
    SgGLInputCallback input;
} RenderSource;

/** Releases the source reference when GTK destroys its callback storage. */
static void release_source(gpointer user_data) {
    sg_ui_require_main_thread();
    RenderSource *source = user_data;
    source->release(source->data);
    g_free(source);
}

/** Draws a submitted frame after GTK makes the area's framebuffer current. */
static gboolean on_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data) {
    (void)context;
    sg_ui_require_main_thread();

    RenderSource *source = user_data;
    GtkWidget *widget = GTK_WIDGET(area);
    int scale = gtk_widget_get_scale_factor(widget);
    int pixel_width = gtk_widget_get_width(widget) * scale;
    int pixel_height = gtk_widget_get_height(widget) * scale;

    // The source records rendering errors; TRUE tells GTK that this signal was handled.
    source->render(source->data, pixel_width, pixel_height);
    return TRUE;
}

/** Resets context-local resources before GTK destroys the area's context. */
static void on_unrealize(GtkWidget *widget, gpointer user_data) {
    sg_ui_require_main_thread();
    RenderSource *source = user_data;
    GtkGLArea *area = GTK_GL_AREA(widget);

    gtk_gl_area_make_current(area);
    gboolean context_available = gtk_gl_area_get_error(area) == NULL;
    source->reset(source->data, context_available);
    gdk_gl_context_clear_current();
}

/** Queues absolute pointer movement in logical viewport coordinates. */
static void on_motion(GtkEventControllerMotion *controller, double x, double y,
                      gpointer user_data) {
    (void)controller;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_MOTION, 0, x, y);
}

/** Focuses the viewport and queues a positive button code for a press. */
static void on_button_pressed(GtkGestureClick *gesture, int press_count, double x, double y,
                              gpointer user_data) {
    (void)press_count;
    RenderSource *source = user_data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int button = (int)gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    gtk_widget_grab_focus(widget);
    source->input(source->data, SOURCE_INPUT_BUTTON, button, x, y);
}

/** Queues a negative button code for a release. */
static void on_button_released(GtkGestureClick *gesture, int press_count, double x, double y,
                               gpointer user_data) {
    (void)press_count;
    RenderSource *source = user_data;
    int button = (int)gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    source->input(source->data, SOURCE_INPUT_BUTTON, -button, x, y);
}

/** Maps GTK key values to the source's Unicode convention, including Escape. */
static int source_key_code(guint key_value) {
    return key_value == GDK_KEY_Escape ? 27 : (int)gdk_keyval_to_unicode(key_value);
}

/** Queues a key press and consumes the viewport's keyboard event. */
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint key_value,
                               guint hardware_code, GdkModifierType modifiers, gpointer user_data) {
    (void)controller;
    (void)hardware_code;
    (void)modifiers;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_KEY, source_key_code(key_value), 1, 0);
    return TRUE;
}

/** Queues a key release without borrowing application state. */
static void on_key_released(GtkEventControllerKey *controller, guint key_value, guint hardware_code,
                            GdkModifierType modifiers, gpointer user_data) {
    (void)controller;
    (void)hardware_code;
    (void)modifiers;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_KEY, source_key_code(key_value), 0, 0);
}

/** Signals focus loss so the application can clear held-input state. */
static void on_focus_leave(GtkEventControllerFocus *controller, gpointer user_data) {
    (void)controller;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_FOCUS, 0, 0, 0);
}

/** Signals focus acquisition independently of semantic UI messages. */
static void on_focus_enter(GtkEventControllerFocus *controller, gpointer user_data) {
    (void)controller;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_FOCUS, 1, 0, 0);
}

/** Queues scroll deltas and consumes the viewport's scroll gesture. */
static gboolean on_scroll(GtkEventControllerScroll *controller, double delta_x, double delta_y,
                          gpointer user_data) {
    (void)controller;
    RenderSource *source = user_data;
    source->input(source->data, SOURCE_INPUT_SCROLL, 0, delta_x, delta_y);
    return TRUE;
}

/** Transfers pointer, button, and scroll controllers to the widget. */
static void attach_pointer_controllers(GtkWidget *widget, RenderSource *source) {
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), source);
    gtk_widget_add_controller(widget, motion);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_button_pressed), source);
    g_signal_connect(click, "released", G_CALLBACK(on_button_released), source);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), source);
    gtk_widget_add_controller(widget, scroll);
}

/** Transfers keyboard and focus controllers to the widget. */
static void attach_keyboard_controllers(GtkWidget *widget, RenderSource *source) {
    GtkEventController *keyboard = gtk_event_controller_key_new();
    g_signal_connect(keyboard, "key-pressed", G_CALLBACK(on_key_pressed), source);
    g_signal_connect(keyboard, "key-released", G_CALLBACK(on_key_released), source);
    gtk_widget_add_controller(widget, keyboard);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), source);
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), source);
    gtk_widget_add_controller(widget, focus);
}

GtkWidget *sg_gl_area_new(void *data, SgGLRenderCallback render, SgGLResetCallback reset,
                          SgGLLifetimeCallback retain, SgGLLifetimeCallback release,
                          SgGLInputCallback input) {
    sg_ui_require_main_thread();
    if (!data || !render || !reset || !retain || !release || !input) {
        return NULL;
    }

    RenderSource *source = g_new0(RenderSource, 1);
    source->data = data;
    source->render = render;
    source->reset = reset;
    source->release = release;
    source->input = input;
    retain(data);

    GtkWidget *widget = gtk_gl_area_new();
    GtkGLArea *area = GTK_GL_AREA(widget);
    g_object_ref_sink(widget);
    gtk_gl_area_set_required_version(area, 3, 3);
    gtk_gl_area_set_allowed_apis(area, GDK_GL_API_GL);
    gtk_gl_area_set_has_depth_buffer(area, TRUE);
    gtk_gl_area_set_auto_render(area, FALSE);
    gtk_widget_set_focusable(widget, TRUE);

    // GTK owns the source reference and controllers until the widget is destroyed.
    g_object_set_data_full(G_OBJECT(widget), "sun-gl-source", source, release_source);
    g_signal_connect(widget, "render", G_CALLBACK(on_render), source);
    g_signal_connect(widget, "unrealize", G_CALLBACK(on_unrealize), source);
    attach_pointer_controllers(widget, source);
    attach_keyboard_controllers(widget, source);
    return widget;
}

void sg_gl_area_queue(GtkWidget *area) {
    sg_ui_require_main_thread();
    gtk_gl_area_queue_render(GTK_GL_AREA(area));
}

const char *sg_gl_area_error(GtkWidget *area) {
    sg_ui_require_main_thread();
    GError *error = gtk_gl_area_get_error(GTK_GL_AREA(area));
    return error ? error->message : "";
}

void sg_gl_clear_current(void) {
    sg_ui_require_main_thread();
    gdk_gl_context_clear_current();
}
