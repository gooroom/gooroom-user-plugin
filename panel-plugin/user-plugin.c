/*
 *  Copyright (C) 2015-2019 Gooroom <gooroom@gooroom.kr>
 *
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <glib-object.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include <act/act.h>


#define DEFAULT_USER_IMAGE_SIZE (32)
#define PANEL_TRAY_ICON_SIZE    (24)



struct _UserPluginClass
{
  XfcePanelPluginClass __parent__;
};

/* plugin structure */
struct _UserPlugin
{
	XfcePanelPlugin      __parent__;

	GtkWidget       *button;
	GtkWidget       *popup_window;
	GtkWidget       *popup_lbl_user;
	GtkWidget       *popup_img_user;

	ActUserManager  *um;
	ActUser         *user;
};

typedef enum {
	ACTION_TYPE_SHUTDOWN      = 1 << 1,
	ACTION_TYPE_LOCK_SCREEN   = 1 << 2,
	ACTION_TYPE_SETTINGS      = 1 << 3
} ActionType;

typedef struct {
  ActionType   type;
  const gchar *widget_name;
  const gchar *icon_name;
  const gchar *display_name;
} ActionEntry;

static ActionEntry action_entries[] = {
  { ACTION_TYPE_SETTINGS,
    "box-settings",
	"system-run-symbolic",
    N_("System Settings")
  },
  { ACTION_TYPE_LOCK_SCREEN,
    "box-lock-screen",
	"system-lock-screen-symbolic",
    N_("Lock Screen")
  },
  { ACTION_TYPE_SHUTDOWN,
    "box-shutdown",
	"system-shutdown-symbolic",
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

			gchar *markup = g_strdup_printf ("<big><b>%s</b></big>", name);
			gtk_label_set_markup (GTK_LABEL (plugin->popup_lbl_user), markup);
			g_free (markup);
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
on_action_item_clicked (GtkButton *button, gpointer data)
{
	GError   *error   = NULL;
	gboolean  succeed = FALSE;
	gint      action  = GPOINTER_TO_INT (data);

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

	if (plugin->popup_window != NULL) {
		gtk_widget_destroy (plugin->popup_window);

		plugin->popup_window = NULL;
		plugin->popup_lbl_user = NULL;
		plugin->popup_img_user = NULL;
    }

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);

	return TRUE;
}

static gboolean
on_popup_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event->type == GDK_KEY_PRESS && event->keyval == GDK_Escape) {
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
item_button_new (const gchar *icon_name, const gchar *text)
{
	GtkWidget *button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (button, FALSE);
	gtk_widget_set_name (button, "gooroom-user-plugin-item-button");

	GtkWidget *hbox = gtk_hbox_new (FALSE, 96);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 2);
	gtk_container_add (GTK_CONTAINER (button), hbox);
	gtk_widget_show (hbox);

	GtkWidget *icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, FALSE, 5);
	gtk_widget_show (icon);

	GtkWidget *label = gtk_label_new (_(text));
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_RIGHT);
	gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 5);
	gtk_widget_show (label);

	return button;
}

static GtkWidget *
popup_window_new (UserPlugin *plugin, GdkEventButton *event)
{
	GtkWidget *window;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint(GTK_WINDOW (window), TRUE);
	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_window_stick(GTK_WINDOW (window));
	gtk_window_set_screen (GTK_WINDOW (window), gtk_widget_get_screen (GTK_WIDGET (plugin)));

	GtkWidget *main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (window), main_vbox);
	gtk_widget_show (main_vbox);

	GtkWidget *ebox = gtk_event_box_new ();
	gtk_widget_set_name (ebox, "panel-popup-window-frame");
	gtk_box_pack_start (GTK_BOX (main_vbox), ebox, FALSE, FALSE, 0);
	gtk_widget_show (ebox);

	GtkWidget *hbox = gtk_hbox_new (FALSE, 9);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_container_add (GTK_CONTAINER (ebox), hbox);
	gtk_widget_show (hbox);

	GtkWidget *button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (button, FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	gtk_widget_show (button);

	plugin->popup_img_user = gtk_image_new_from_icon_name ("avatar-default-symbolic", GTK_ICON_SIZE_DND);
	gtk_container_add (GTK_CONTAINER (button), plugin->popup_img_user);
	gtk_widget_show (plugin->popup_img_user);

	plugin->popup_lbl_user = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (plugin->popup_lbl_user), "<b>Guest</b>");
	gtk_box_pack_start (GTK_BOX (hbox), plugin->popup_lbl_user, FALSE, TRUE, 0);
	gtk_widget_show (plugin->popup_lbl_user);

	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (on_user_face_button_clicked), plugin);

	GtkWidget *item_box = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (item_box), 0);
	gtk_container_add (GTK_CONTAINER (main_vbox), item_box);
	gtk_widget_show (item_box);

	gboolean loaded = FALSE;
	g_object_get (plugin->um, "is-loaded", &loaded, NULL);
	if (loaded)
		user_info_update (plugin->um, NULL, plugin);
	else
		g_signal_connect (plugin->um, "notify::is-loaded", G_CALLBACK (user_info_update), plugin);

	ActionType allowed_types = allowed_actions_type_get ();

	guint i;
	for (i = 0; i < G_N_ELEMENTS (action_entries); i++) {
		if (allowed_types & action_entries[i].type) {
			GtkWidget *item = item_button_new (action_entries[i].icon_name, action_entries[i].display_name);
			gtk_box_pack_start (GTK_BOX (item_box), item, FALSE, FALSE, 0);
			gtk_widget_show_all (item);
			g_signal_connect (G_OBJECT (item), "clicked",
                              G_CALLBACK (on_action_item_clicked),
                              GINT_TO_POINTER (action_entries[i].type));
		}
	}

	g_signal_connect (G_OBJECT (window), "realize", G_CALLBACK (on_popup_window_realized), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "delete-event", G_CALLBACK (on_popup_window_closed), plugin);
	g_signal_connect (G_OBJECT (window), "key-press-event", G_CALLBACK (on_popup_key_press_event), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "focus-out-event", G_CALLBACK (on_popup_window_closed), plugin);

	gtk_widget_set_size_request (window, 192, -1);

	gtk_widget_show_all (window);

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);

	return window;
}

static gboolean
on_plugin_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	UserPlugin *plugin = USER_PLUGIN (data);

	if (event->button == 1 || event->button == 2) {
		if (event->type == GDK_BUTTON_PRESS) {
			if (plugin->popup_window != NULL) {
				on_popup_window_closed (plugin);
			} else {
				plugin->popup_window = popup_window_new (plugin, event);
			}

			return TRUE;
		}
	}

	/* bypass GTK_TOGGLE_BUTTON's handler and go directly to the plugin's one */
	return (*GTK_WIDGET_CLASS (user_plugin_parent_class)->button_press_event) (GTK_WIDGET (plugin), event);
}

static gboolean
update_ui (gpointer data)
{
	UserPlugin *plugin = USER_PLUGIN (data);

	plugin->um = act_user_manager_get_default ();

	g_signal_connect (G_OBJECT (plugin->button), "button-press-event", G_CALLBACK (on_plugin_button_pressed), plugin);

	return FALSE;
}

static void
user_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
	UserPlugin *plugin = USER_PLUGIN (panel_plugin);

	if (plugin->popup_window != NULL)
		on_popup_window_closed (plugin);
}

static gboolean
user_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                          gint             size)
{
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
	plugin->user         = NULL;
	plugin->button       = NULL;
	plugin->um           = NULL;

	xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

	plugin->button = xfce_panel_create_toggle_button ();
	xfce_panel_plugin_add_action_widget (XFCE_PANEL_PLUGIN (plugin), plugin->button);
	gtk_container_add (GTK_CONTAINER (plugin), plugin->button);

	GdkPixbuf *pix;
	pix = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                    "user-plugin-panel",
                                    PANEL_TRAY_ICON_SIZE,
                                    GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

	if (pix) {
		GtkWidget *tray = gtk_image_new_from_pixbuf (pix);
		gtk_image_set_pixel_size (GTK_IMAGE (tray), PANEL_TRAY_ICON_SIZE);
		gtk_container_add (GTK_CONTAINER (plugin->button), tray);
		g_object_unref (G_OBJECT (pix));
	}

	gtk_widget_show_all (plugin->button);

	g_timeout_add (500, (GSourceFunc) update_ui, plugin);
}

static void
user_plugin_class_init (UserPluginClass *klass)
{
	XfcePanelPluginClass *plugin_class;

	plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
	plugin_class->free_data = user_plugin_free_data;
	plugin_class->size_changed = user_plugin_size_changed;
	plugin_class->mode_changed = user_plugin_mode_changed;
}
