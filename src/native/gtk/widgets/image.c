#include "gtk/widgets/image.h"
#include "gtk/runtime/ui.h"

/** Owns the cached source path while GTK owns the child widgets and retained paintable. */
typedef struct {
    GtkWidget *picture;
    GtkWidget *notice;
    char *path;
    int status;
} Image;

/** Releases path storage after the parent widget releases its child hierarchy. */
static void image_free(gpointer data) {
    Image *image = data;
    g_free(image->path);
    g_free(image);
}

GtkWidget *sg_image_new(void) {
    sg_ui_require_main_thread();
    Image *image = g_new0(Image, 1);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *viewport = gtk_overlay_new();
    gtk_widget_set_size_request(viewport, 160, 120);
    gtk_widget_set_vexpand(viewport, TRUE);
    image->picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(image->picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(image->picture), TRUE);
    gtk_picture_set_alternative_text(GTK_PICTURE(image->picture), "Image preview");
    gtk_widget_set_halign(image->picture, GTK_ALIGN_FILL);
    gtk_widget_set_valign(image->picture, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(viewport), image->picture);
    gtk_box_append(GTK_BOX(root), viewport);
    image->notice = gtk_label_new("Image preview unavailable.");
    gtk_label_set_wrap(GTK_LABEL(image->notice), TRUE);
    gtk_widget_set_visible(image->notice, FALSE);
    gtk_box_append(GTK_BOX(root), image->notice);
    g_object_set_data_full(G_OBJECT(root), "sun-image", image, image_free);
    return root;
}

void sg_image_set(GtkWidget *widget, const char *path) {
    sg_ui_require_main_thread();
    Image *image = g_object_get_data(G_OBJECT(widget), "sun-image");
    if (!image || g_strcmp0(image->path, path) == 0) return;
    g_free(image->path);
    image->path = g_strdup(path);
    GError *error = NULL;
    GdkTexture *texture = path && *path ? gdk_texture_new_from_filename(path, &error) : NULL;
    gtk_picture_set_paintable(GTK_PICTURE(image->picture), texture ? GDK_PAINTABLE(texture) : NULL);
    image->status = texture ? 1 : (path && *path ? -1 : 0);
    gtk_widget_set_visible(image->notice, image->status < 0);
    g_clear_object(&texture);
    g_clear_error(&error);
}

int sg_image_status(GtkWidget *widget) {
    sg_ui_require_main_thread();
    Image *image = g_object_get_data(G_OBJECT(widget), "sun-image");
    return image ? image->status : -1;
}
