/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "user-plugin.h"
#include "user-window-resources.h"

#include <gtk/gtk.h>
#include <glib-object.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <act/act.h>



#define DEFAULT_USER_IMAGE_SIZE (32)

#define GET_WIDGET(builder, x) GTK_WIDGET (gtk_builder_get_object (builder, x))

struct _UserPluginClass
{
  XfcePanelPluginClass __parent__;
};

/* plugin structure */
struct _UserPlugin
{
	XfcePanelPlugin      __parent__;

	GtkWidget       *button;
	GtkWidget       *img_tray;
	GtkWidget       *popup_window;
	GtkWidget       *popup_lbl_user;
	GtkWidget       *popup_img_user;

	ActUserManager  *um;
	ActUser         *user;

	GtkBuilder      *builder;
};

typedef enum {
	ACTION_TYPE_SHUTDOWN      = 1 << 1,
	ACTION_TYPE_LOCK_SCREEN   = 1 << 2,
	ACTION_TYPE_SETTINGS      = 1 << 3
} ActionType;

typedef struct {
  ActionType   type;
  const gchar *widget_name;
  const gchar *display_name;
} ActionEntry;

static ActionEntry action_entries[] = {
  { ACTION_TYPE_SETTINGS,
    "box-settings",
    N_("System Settings")
  },
  { ACTION_TYPE_LOCK_SCREEN,
    "box-lock-screen",
    N_("Lock Screen")
  },
  { ACTION_TYPE_SHUTDOWN,
    "box-shutdown",
    N_("System Shutdown")
  }
};

/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (UserPlugin, user_plugin)

static GdkPixbuf *
get_user_face (const gchar *icon, gint size)
{
	GdkPixbuf *face = NULL;

	if (icon) {
		face = gdk_pixbuf_new_from_file_at_scale (icon, size, size, TRUE, NULL);
	}

	if (!face) {
		face = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
				"avatar-default",
				size, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
	}

	return face;
}

static void
user_info_update (ActUserManager *um, GParamSpec *pspec, gpointer data)
{
	UserPlugin *plugin = USER_PLUGIN (data);

	if (!act_user_manager_no_service (um)) {
		ActUser *user   = NULL;
		user = act_user_manager_get_user_by_id (um, getuid ());
		if (user) {
			const gchar *icon = act_user_get_icon_file (user);
			GdkPixbuf *face = get_user_face (icon, DEFAULT_USER_IMAGE_SIZE);
			gtk_image_set_from_pixbuf (GTK_IMAGE (plugin->popup_img_user), face);
			g_object_unref (face);

			const gchar *name = act_user_get_real_name (user);
			if (name == NULL) {
				name = act_user_get_user_name (user);
			}

			gtk_label_set_text (GTK_LABEL (plugin->popup_lbl_user), name);
		}
	}
}

static ActionType
allowed_actions_type_get (void)
{
	gchar      *path;
	ActionType  allow_mask = ACTION_TYPE_SHUTDOWN;

	/* check for commands we use */
	path = g_find_program_in_path ("xflock4");
	if (path != NULL)
		allow_mask |= ACTION_TYPE_LOCK_SCREEN;
	g_free (path);

	path = g_find_program_in_path ("gooroom-control-center");
	if (path != NULL)
		allow_mask |= ACTION_TYPE_SETTINGS;
	g_free (path);

	return allow_mask;
}

static void
on_row_activated (GtkListBox *listbox, GtkListBoxRow *row)
{
	GError        *error = NULL;
	gboolean       succeed = FALSE;
	gint           action;

	action = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "action"));

	switch (action)
	{
		case ACTION_TYPE_SHUTDOWN:
			succeed = g_spawn_command_line_async ("xfce4-session-logout", &error);
			break;

		case ACTION_TYPE_LOCK_SCREEN:
			succeed = g_spawn_command_line_async ("xflock4", &error);
			break;

		case ACTION_TYPE_SETTINGS:
			succeed = g_spawn_command_line_async ("gooroom-control-center", &error);
			break;

		default:
			return;
	}

	if (!succeed) {
		xfce_dialog_show_error (NULL, error,
								_("Failed to run action \"%s\""),
								action_entries[action].display_name);
	}
}

static gboolean
on_popup_window_closed (gpointer data)
{
	UserPlugin *plugin = USER_PLUGIN (data);

	gtk_widget_destroy (plugin->popup_window);

	plugin->popup_window = NULL;
	plugin->popup_lbl_user = NULL;
	plugin->popup_img_user = NULL;

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);

	return TRUE;
}

static gboolean
on_popup_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_Escape) {
		on_popup_window_closed (data);
		return TRUE;
	}

	return FALSE;
}

static void
on_user_face_button_clicked (GtkButton *button, gpointer data)
{
	GtkWidget *dialog;

	UserPlugin *plugin = USER_PLUGIN (data);

	dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW (plugin->popup_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			_("_Cancel"),
			GTK_RESPONSE_CANCEL,
			_("_Open"),
			GTK_RESPONSE_ACCEPT,
			NULL);

	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, "Image"); 
	gtk_file_filter_add_mime_type (filter, "image/*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

	g_signal_handlers_block_by_func (G_OBJECT (plugin->popup_window), on_popup_window_closed, plugin);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));    
		if (!act_user_manager_no_service (plugin->um)) {
			ActUser *user = act_user_manager_get_user_by_id (plugin->um, getuid ());
			if (user) {
				act_user_set_icon_file (user, filename);

				if (plugin->popup_window) {
					GdkPixbuf *face = get_user_face (filename, DEFAULT_USER_IMAGE_SIZE);
					gtk_image_set_from_pixbuf (GTK_IMAGE (plugin->popup_img_user), face);
					g_object_unref (face);
				}
			}
		}
		g_free (filename);
	}

	g_signal_handlers_unblock_by_func (G_OBJECT (plugin->popup_window), on_popup_window_closed, plugin);

	gtk_window_present (GTK_WINDOW (plugin->popup_window));

	gtk_widget_destroy (dialog);
}

static void
on_popup_window_realized (GtkWidget *widget, gpointer data)
{
	gint x, y;
	UserPlugin *plugin = USER_PLUGIN (data);

	xfce_panel_plugin_position_widget (XFCE_PANEL_PLUGIN (plugin), widget, plugin->button, &x, &y);
	gtk_window_move (GTK_WINDOW (widget), x, y);
}

static GtkWidget *
popup_user_window (UserPlugin *plugin)
{
	gint      i;
	GError    *error = NULL;
	GdkScreen *screen;
	GtkWidget *window;
	GtkWidget *btn_user;
	GtkWidget *listbox;
	ActionType allowed_types;
	gboolean   loaded = FALSE;

    gtk_builder_add_from_resource (plugin->builder, "/kr/gooroom/user/plugin/user-window.ui", NULL);
	if (error) {
		g_error_free (error);
		return NULL;
	}

	window = GET_WIDGET (plugin->builder, "user-window");
	gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint(GTK_WINDOW (window), TRUE);
	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_window_stick(GTK_WINDOW (window));

	screen = gtk_widget_get_screen (GTK_WIDGET (plugin->button));
	gtk_window_set_screen (GTK_WINDOW (window), screen);

	g_signal_connect (G_OBJECT (window), "realize", G_CALLBACK (on_popup_window_realized), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "delete-event", G_CALLBACK (on_popup_window_closed), plugin);
	g_signal_connect (G_OBJECT (window), "key-press-event", G_CALLBACK (on_popup_key_press_event), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "focus-out-event", G_CALLBACK (on_popup_window_closed), plugin);

	listbox = GET_WIDGET (plugin->builder, "listbox");
	btn_user = GET_WIDGET (plugin->builder, "btn-user");
	plugin->popup_lbl_user = GET_WIDGET (plugin->builder, "lbl-user");
	plugin->popup_img_user = GET_WIDGET (plugin->builder, "img-user");

	g_object_get (plugin->um, "is-loaded", &loaded, NULL);
	if (loaded)
		user_info_update (plugin->um, NULL, plugin);
	else
		g_signal_connect (plugin->um, "notify::is-loaded", G_CALLBACK (user_info_update), plugin);

	allowed_types = allowed_actions_type_get ();

	for (i = 0; i < G_N_ELEMENTS (action_entries); i++) {
		GtkWidget *w = GET_WIDGET (plugin->builder, action_entries[i].widget_name);
		if (w && (allowed_types & action_entries[i].type)) {
			GtkWidget *row = gtk_list_box_row_new (); 
			gtk_container_add (GTK_CONTAINER (row), w);
			gtk_container_add (GTK_CONTAINER (listbox), GTK_WIDGET (row));
			gtk_widget_show_all (row);
			g_object_set_data (G_OBJECT (row), "action", GINT_TO_POINTER (action_entries[i].type));
		}
	}

	g_signal_connect (G_OBJECT (listbox), "row-activated", G_CALLBACK (on_row_activated), NULL);
	g_signal_connect (G_OBJECT (btn_user), "clicked", G_CALLBACK (on_user_face_button_clicked), plugin);

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);

	gtk_widget_show (window);

	return window;
}

static gboolean
on_user_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	UserPlugin *plugin = USER_PLUGIN (data);

	if (event->button == 1 || event->button == 2) {
		if (event->type == GDK_BUTTON_PRESS) {
			if (plugin->popup_window != NULL) {
				on_popup_window_closed (plugin);
			} else {
				plugin->popup_window = popup_user_window (plugin);
			}

			return TRUE;
		}
	}

	/* bypass GTK_TOGGLE_BUTTON's handler and go directly to the plugin's one */
	return (*GTK_WIDGET_CLASS (user_plugin_parent_class)->button_press_event) (GTK_WIDGET (plugin), event);
}

static void
user_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
	UserPlugin *plugin = USER_PLUGIN (panel_plugin);

	if (plugin->builder)
		g_object_unref (plugin->builder);
}

static gboolean
user_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                          gint             size)
{
	gboolean loaded = FALSE;
	UserPlugin *plugin = USER_PLUGIN (panel_plugin);

	/* set the clock size */
	if (xfce_panel_plugin_get_mode (panel_plugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL) {
		gtk_widget_set_size_request (GTK_WIDGET (panel_plugin), -1, size);
	} else {
		gtk_widget_set_size_request (GTK_WIDGET (panel_plugin), size, -1);
	}

	return TRUE;
}

static void
user_plugin_mode_changed (XfcePanelPlugin *plugin, XfcePanelPluginMode mode)
{
	user_plugin_size_changed (plugin, xfce_panel_plugin_get_size (plugin));
}

static void
user_plugin_init (UserPlugin *plugin)
{
	GError *error        = NULL;

	plugin->user         = NULL;
	plugin->button       = NULL;
	plugin->img_tray     = NULL;
	plugin->um           = NULL;

	g_resources_register (user_window_get_resource ());

	plugin->um = act_user_manager_get_default ();

	plugin->builder = gtk_builder_new ();
}

static void
user_plugin_construct (XfcePanelPlugin *panel_plugin)
{
	GtkWidget *image;
	gboolean loaded = FALSE;

	UserPlugin *plugin = USER_PLUGIN (panel_plugin);

	xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

	plugin->button = xfce_panel_create_toggle_button ();
	gtk_widget_set_name (plugin->button, "user-plugin-button");
	gtk_button_set_relief (GTK_BUTTON (plugin->button), GTK_RELIEF_NONE);
	gtk_container_add (GTK_CONTAINER (plugin), plugin->button);
	xfce_panel_plugin_add_action_widget (XFCE_PANEL_PLUGIN (plugin), plugin->button);
	gtk_widget_show (plugin->button);

	plugin->img_tray = gtk_image_new ();
	gtk_container_add (GTK_CONTAINER (plugin->button), plugin->img_tray);
	gtk_widget_show (plugin->img_tray);

	gtk_image_set_from_icon_name (GTK_IMAGE (plugin->img_tray), "user-plugin-symbolic", GTK_ICON_SIZE_BUTTON);
	gtk_image_set_pixel_size (GTK_IMAGE (plugin->img_tray), 22);

	g_signal_connect (G_OBJECT (plugin->button), "button-press-event", G_CALLBACK (on_user_button_pressed), plugin);
}

static void
user_plugin_class_init (UserPluginClass *klass)
{
	XfcePanelPluginClass *plugin_class;

	plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
	plugin_class->construct = user_plugin_construct;
	plugin_class->free_data = user_plugin_free_data;
	plugin_class->size_changed = user_plugin_size_changed;
	plugin_class->mode_changed = user_plugin_mode_changed;
}
