#include "view.h"
#include "ui.h"
#include <string.h>

/** Owns one automatic, fixed, or weighted grid track. */
typedef struct { int kind; int value; } GridTrack;
/** Stores the grid coordinates associated with one child. */
typedef struct { int row; int column; int rows; int columns; } GridCell;
/** Provides a grid whose weighted tracks share remaining allocation. */
typedef struct { GtkWidget parent; GArray *rows; GArray *columns; int spacing; } SgGrid;
/** Registers allocation and measurement for the custom grid widget. */
typedef struct { GtkWidgetClass parent; } SgGridClass;
/** Declares the native grid type registration function. */
static GType sg_grid_get_type(void);
G_DEFINE_TYPE(SgGrid, sg_grid, GTK_TYPE_WIDGET)

/** Returns track minima or natural sizes after measuring single and spanning children. */
static int *grid_sizes(SgGrid *grid, GtkOrientation orientation, gboolean natural, int for_size) {
    GArray *tracks = orientation == GTK_ORIENTATION_HORIZONTAL ? grid->columns : grid->rows;
    int *sizes = g_new0(int, MAX(tracks->len, 1));
    for (guint i = 0; i < tracks->len; i++) {
        GridTrack track = g_array_index(tracks, GridTrack, i);
        if (track.kind == 1) sizes[i] = track.value;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(grid)); child; child = gtk_widget_get_next_sibling(child)) {
        if (!gtk_widget_should_layout(child)) continue;
        GridCell *cell = g_object_get_data(G_OBJECT(child), "sun-grid-cell");
        if (!cell) continue;
        int start = orientation == GTK_ORIENTATION_HORIZONTAL ? cell->column : cell->row;
        int span = orientation == GTK_ORIENTATION_HORIZONTAL ? cell->columns : cell->rows;
        if (start < 0 || span < 1 || (guint)(start + span) > tracks->len) continue;
        int minimum, preferred;
        gtk_widget_measure(child, orientation, for_size, &minimum, &preferred, NULL, NULL);
        int have = grid->spacing * (span - 1), flexible = 0;
        for (int j = start; j < start + span; j++) {
            have += sizes[j];
            if (g_array_index(tracks, GridTrack, j).kind != 1) flexible++;
        }
        int deficit = MAX(0, (natural ? preferred : minimum) - have);
        for (int j = start; j < start + span && flexible; j++) {
            if (g_array_index(tracks, GridTrack, j).kind != 1) {
                int share = (deficit + flexible - 1) / flexible;
                sizes[j] += share; deficit -= share; flexible--;
            }
        }
    }
    return sizes;
}
/** Reports the sum of track requests and gaps. */
static void sg_grid_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline) {
    SgGrid *grid = (SgGrid *)widget;
    GArray *tracks = orientation == GTK_ORIENTATION_HORIZONTAL ? grid->columns : grid->rows;
    int *small = grid_sizes(grid, orientation, FALSE, for_size);
    int *large = grid_sizes(grid, orientation, TRUE, for_size);
    *minimum = *natural = grid->spacing * MAX(0, (int)tracks->len - 1);
    for (guint i = 0; i < tracks->len; i++) { *minimum += small[i]; *natural += large[i]; }
    *minimum_baseline = *natural_baseline = -1;
    g_free(small); g_free(large);
}
/** Distributes surplus space proportionally among weighted tracks. */
static int *grid_allocate_tracks(SgGrid *grid, GtkOrientation orientation, int available, int other_size) {
    GArray *tracks = orientation == GTK_ORIENTATION_HORIZONTAL ? grid->columns : grid->rows;
    int *sizes = grid_sizes(grid, orientation, TRUE, other_size);
    gint64 total = grid->spacing * MAX(0, (int)tracks->len - 1), weight = 0;
    for (guint i = 0; i < tracks->len; i++) {
        total += sizes[i];
        GridTrack track = g_array_index(tracks, GridTrack, i);
        if (track.kind == 2) weight += track.value;
    }
    gint64 extra = MAX(0, available - total);
    for (guint i = 0; i < tracks->len && weight; i++) {
        GridTrack track = g_array_index(tracks, GridTrack, i);
        if (track.kind == 2) {
            int share = (int)(extra * track.value / weight);
            sizes[i] += share; extra -= share; weight -= track.value;
        }
    }
    return sizes;
}
/** Allocates each child to its spanning rectangle without recreating widgets. */
static void sg_grid_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    (void)baseline;
    SgGrid *grid = (SgGrid *)widget;
    int *columns = grid_allocate_tracks(grid, GTK_ORIENTATION_HORIZONTAL, width, height);
    int *rows = grid_allocate_tracks(grid, GTK_ORIENTATION_VERTICAL, height, width);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        if (!gtk_widget_should_layout(child)) continue;
        GridCell *cell = g_object_get_data(G_OBJECT(child), "sun-grid-cell");
        if (!cell || (guint)(cell->column + cell->columns) > grid->columns->len || (guint)(cell->row + cell->rows) > grid->rows->len) continue;
        int x = 0, y = 0, w = 0, h = 0;
        for (int i = 0; i < cell->column; i++) x += columns[i] + grid->spacing;
        for (int i = 0; i < cell->row; i++) y += rows[i] + grid->spacing;
        for (int i = cell->column; i < cell->column + cell->columns; i++) w += columns[i];
        for (int i = cell->row; i < cell->row + cell->rows; i++) h += rows[i];
        w += grid->spacing * (cell->columns - 1); h += grid->spacing * (cell->rows - 1);
        graphene_point_t point = GRAPHENE_POINT_INIT(x, y);
        gtk_widget_allocate(child, MAX(0, w), MAX(0, h), -1, gsk_transform_translate(NULL, &point));
    }
    g_free(columns); g_free(rows);
}
/** Draws each visible child using its allocated transform. */
static void sg_grid_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        if (gtk_widget_get_visible(child)) gtk_widget_snapshot_child(widget, child, snapshot);
    }
}
/** Detaches children before releasing track storage. */
static void sg_grid_dispose(GObject *object) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(object)))) gtk_widget_unparent(child);
    SgGrid *grid = (SgGrid *)object;
    g_clear_pointer(&grid->rows, g_array_unref); g_clear_pointer(&grid->columns, g_array_unref);
    G_OBJECT_CLASS(sg_grid_parent_class)->dispose(object);
}
/** Installs grid layout, drawing, and lifetime behavior. */
static void sg_grid_class_init(SgGridClass *klass) {
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->measure = sg_grid_measure; widget->size_allocate = sg_grid_size_allocate; widget->snapshot = sg_grid_snapshot;
    G_OBJECT_CLASS(klass)->dispose = sg_grid_dispose;
}
/** Starts with one automatic row and column. */
static void sg_grid_init(SgGrid *grid) {
    grid->rows = g_array_new(FALSE, FALSE, sizeof(GridTrack)); grid->columns = g_array_new(FALSE, FALSE, sizeof(GridTrack));
    GridTrack automatic = {0, 0};
    g_array_append_val(grid->rows, automatic); g_array_append_val(grid->columns, automatic);
}

GtkWidget *sg_view_new(int kind) {
    sg_ui_require_main_thread();
    GtkWidget *widget = NULL;
    switch (kind) {
    case 0: case 2: widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); break;
    case 1: widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); break;
    case 3: widget = g_object_new(sg_grid_get_type(), NULL); break;
    case 4: widget = gtk_scrolled_window_new(); break;
    case 5: widget = gtk_label_new(""); break;
    case 6: widget = gtk_button_new_with_label(""); break;
    case 7: widget = gtk_entry_new(); break;
    case 8: widget = gtk_check_button_new(); break;
    case 9: widget = gtk_drop_down_new(NULL, NULL); break;
    default: return NULL;
    }
    return g_object_ref_sink(widget);
}
void sg_view_text(GtkWidget *widget, const char *text) {
    g_object_set_data(G_OBJECT(widget), "sun-applying", GINT_TO_POINTER(1));
    if (GTK_IS_BUTTON(widget)) gtk_button_set_label(GTK_BUTTON(widget), text);
    else if (GTK_IS_LABEL(widget)) gtk_label_set_text(GTK_LABEL(widget), text);
    else if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), text);
    else if (GTK_IS_EDITABLE(widget) && strcmp(gtk_editable_get_text(GTK_EDITABLE(widget)), text)) gtk_editable_set_text(GTK_EDITABLE(widget), text);
    g_object_set_data(G_OBJECT(widget), "sun-applying", NULL);
}
void sg_view_value(GtkWidget *widget, int value) {
    g_object_set_data(G_OBJECT(widget), "sun-applying", GINT_TO_POINTER(1));
    if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_active(GTK_CHECK_BUTTON(widget), value != 0);
    else if (GTK_IS_DROP_DOWN(widget)) gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), value < 0 ? GTK_INVALID_LIST_POSITION : (guint)value);
    g_object_set_data(G_OBJECT(widget), "sun-applying", NULL);
}
void sg_view_options(GtkWidget *widget, const char *text, const char *key, gboolean clear) {
    if (!GTK_IS_DROP_DOWN(widget)) return;
    g_object_set_data(G_OBJECT(widget), "sun-applying", GINT_TO_POINTER(1));
    if (clear) {
        GtkStringList *list = gtk_string_list_new(NULL);
        gtk_drop_down_set_model(GTK_DROP_DOWN(widget), G_LIST_MODEL(list));
        g_object_unref(list);
    } else {
        GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
        gtk_string_list_append(GTK_STRING_LIST(model), text);
        GObject *item = g_list_model_get_item(model, g_list_model_get_n_items(model) - 1);
        g_object_set_data_full(item, "sun-option-key", g_strdup(key), g_free);
        g_object_unref(item);
    }
    g_object_set_data(G_OBJECT(widget), "sun-applying", NULL);
}
void sg_view_properties(GtkWidget *widget, gboolean enabled, gboolean visible, int margin, int width, int height, int horizontal, int vertical, gboolean expand_x, gboolean expand_y, int spacing) {
    gtk_widget_set_sensitive(widget, enabled);
    gtk_widget_set_visible(widget, visible);
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
    gtk_widget_set_size_request(widget, width, height);
    gtk_widget_set_halign(widget, horizontal);
    gtk_widget_set_valign(widget, vertical);
    gtk_widget_set_hexpand(widget, expand_x);
    gtk_widget_set_vexpand(widget, expand_y);
    if (GTK_IS_BOX(widget)) gtk_box_set_spacing(GTK_BOX(widget), spacing);
    if (G_TYPE_CHECK_INSTANCE_TYPE(widget, sg_grid_get_type())) { ((SgGrid *)widget)->spacing = spacing; gtk_widget_queue_resize(widget); }
}
gboolean sg_view_place(GtkWidget *parent, GtkWidget *child, GtkWidget *previous, int row, int column, int row_span, int column_span) {
    if (parent == child || gtk_widget_is_ancestor(parent, child)) return FALSE;
    GtkWidget *old = gtk_widget_get_parent(child);
    if (old && old != parent && !(GTK_IS_SCROLLED_WINDOW(parent) && GTK_IS_VIEWPORT(old) && gtk_widget_get_parent(old) == parent)) return FALSE;
    if (previous && (previous == child || gtk_widget_get_parent(previous) != parent)) return FALSE;
    if (GTK_IS_BOX(parent)) {
        if (!old) gtk_box_append(GTK_BOX(parent), child);
        if (gtk_widget_get_prev_sibling(child) != previous) gtk_box_reorder_child_after(GTK_BOX(parent), child, previous);
    } else if (G_TYPE_CHECK_INSTANCE_TYPE(parent, sg_grid_get_type())) {
        SgGrid *grid = (SgGrid *)parent;
        if (row < 0 || column < 0 || row_span < 1 || column_span < 1 || (gint64)row + row_span > grid->rows->len || (gint64)column + column_span > grid->columns->len) return FALSE;
        GridCell *cell = g_new(GridCell, 1);
        *cell = (GridCell){row, column, row_span, column_span};
        g_object_set_data_full(G_OBJECT(child), "sun-grid-cell", cell, g_free);
        gtk_widget_insert_after(child, parent, previous);
        gtk_widget_queue_resize(parent);
    } else if (GTK_IS_SCROLLED_WINDOW(parent)) {
        if (previous) return FALSE;
        if (!old) {
            if (gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(parent))) return FALSE;
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), child);
        }
    } else return FALSE;
    return TRUE;
}
void sg_view_remove(GtkWidget *child) {
    GtkWidget *parent = gtk_widget_get_parent(child);
    if (!parent) return;
    if (GTK_IS_BOX(parent)) gtk_box_remove(GTK_BOX(parent), child);
    else if (G_TYPE_CHECK_INSTANCE_TYPE(parent, sg_grid_get_type())) { gtk_widget_unparent(child); gtk_widget_queue_resize(parent); }
    else if (GTK_IS_SCROLLED_WINDOW(parent)) gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), NULL);
    else if (GTK_IS_VIEWPORT(parent)) {
        GtkWidget *scroller = gtk_widget_get_parent(parent);
        if (GTK_IS_SCROLLED_WINDOW(scroller)) gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), NULL);
    }
}
gboolean sg_view_window(GtkWindow *window, GtkWidget *child) {
    if (gtk_widget_get_parent(child)) return FALSE;
    gtk_window_set_child(window, child);
    return TRUE;
}

/** Captures CSS parse failures without allowing GTK diagnostics to terminate the application. */
static void style_error(GtkCssProvider *provider, GtkCssSection *section, const GError *error, gpointer data) {
    (void)provider; (void)section; (void)error;
    *(gboolean *)data = TRUE;
}
/** Owns a display provider until its styled widget is finalized. */
typedef struct {
    GdkDisplay *display;
    GtkCssProvider *provider;
} WidgetStyle;

/** Removes a widget-specific provider before releasing its display reference. */
static void widget_style_free(gpointer data) {
    WidgetStyle *style = data;
    gtk_style_context_remove_provider_for_display(style->display, GTK_STYLE_PROVIDER(style->provider));
    g_object_unref(style->provider);
    g_object_unref(style->display);
    g_free(style);
}
gboolean sg_view_style(GtkWidget *widget, const char *css) {
    GtkCssProvider *provider = gtk_css_provider_new();
    char *name = g_strdup_printf("sun-node-%p", (void *)widget);
    char *selector = g_strconcat(".", name, NULL);
    char **parts = g_strsplit(css, "*", -1);
    char *scoped = g_strjoinv(selector, parts);
    gboolean failed = FALSE;
    gulong handler = g_signal_connect(provider, "parsing-error", G_CALLBACK(style_error), &failed);
    gtk_css_provider_load_from_string(provider, scoped);
    g_signal_handler_disconnect(provider, handler);
    g_strfreev(parts); g_free(scoped); g_free(selector);
    if (failed) { g_free(name); g_object_unref(provider); return FALSE; }
    gtk_widget_add_css_class(widget, name);
    g_free(name);
    WidgetStyle *style = g_new(WidgetStyle, 1);
    style->display = g_object_ref(gtk_widget_get_display(widget));
    style->provider = provider;
    gtk_style_context_add_provider_for_display(style->display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data_full(G_OBJECT(widget), "sun-style", style, widget_style_free);
    return TRUE;
}

gboolean sg_view_track(GtkWidget *widget, gboolean horizontal, int kind, int value, gboolean clear) {
    if (!G_TYPE_CHECK_INSTANCE_TYPE(widget, sg_grid_get_type()) || kind < 0 || kind > 2 || (kind != 0 && value <= 0)) return FALSE;
    SgGrid *grid = (SgGrid *)widget;
    GArray *tracks = horizontal ? grid->columns : grid->rows;
    if (clear) g_array_set_size(tracks, 0);
    else { GridTrack track = {kind, value}; g_array_append_val(tracks, track); }
    gtk_widget_queue_resize(widget);
    return TRUE;
}

void sg_view_edit(GtkWidget *widget, const char *text, int value) {
    if (GTK_IS_EDITABLE(widget)) {
        g_object_set_data(G_OBJECT(widget), "sun-applying", GINT_TO_POINTER(1));
        gtk_editable_set_text(GTK_EDITABLE(widget), text);
        g_object_set_data(G_OBJECT(widget), "sun-applying", NULL);
        g_signal_emit_by_name(widget, "changed");
    }
    else if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_active(GTK_CHECK_BUTTON(widget), value != 0);
    else if (GTK_IS_DROP_DOWN(widget)) gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), value < 0 ? GTK_INVALID_LIST_POSITION : (guint)value);
}
int sg_view_extent(GtkWidget *widget, gboolean horizontal) {
    return horizontal ? gtk_widget_get_width(widget) : gtk_widget_get_height(widget);
}
