/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Shortcut preferences dialog and editor.
 */

/* Shortcuts dialog and shortcut editor UI. */

#include <adwaita.h>
#include <gtk/gtk.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"
#include "internal/shortcuts_registry.h"

typedef struct {
	LdsTerminal *terminal;
	const LdsTerminalShortcutBinding *binding;
	GtkWidget *button;
} LdsTerminalShortcutRowData;

typedef struct {
	LdsTerminalShortcutRowData *row;
	AdwAlertDialog *dialog;
	GtkWidget *preview;
	gchar *captured_accel;
} LdsTerminalShortcutCaptureData;

static GtkWidget *
lds_terminal_shortcuts_make_editable_row(LdsTerminal *terminal,
										 const LdsTerminalShortcutBinding *binding);
static void lds_terminal_shortcuts_refresh_row(LdsTerminalShortcutRowData *row_data);
static char *lds_terminal_shortcuts_label_from_accel(const char *accel);
static gboolean lds_terminal_shortcuts_capture_key_pressed(GtkEventControllerKey *controller,
														   guint keyval, guint keycode,
														   GdkModifierType state,
														   gpointer user_data);
static void lds_terminal_shortcuts_capture_response(AdwAlertDialog *dialog, const char *response,
													gpointer user_data);
static void lds_terminal_shortcuts_capture_data_free(gpointer user_data, GClosure *closure);
static void lds_terminal_on_shortcut_row_button_clicked(GtkButton *button, gpointer user_data);

void lds_terminal_shortcuts_show(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;

	AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());
	adw_preferences_dialog_set_search_enabled(dialog, FALSE);
	adw_dialog_set_presentation_mode(ADW_DIALOG(dialog), ADW_DIALOG_FLOATING);

	AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
	adw_preferences_page_set_title(page, "Shortcuts");

	AdwPreferencesGroup *window_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(window_group, "Window");
	AdwPreferencesGroup *editing_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(editing_group, "Editing");
	AdwPreferencesGroup *view_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(view_group, "View");
	AdwPreferencesGroup *pane_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(pane_group, "Pane");

	for (guint i = 0; i < lds_terminal_shortcuts_count(); i++) {
		const LdsTerminalShortcutBinding *binding = lds_terminal_shortcuts_at(i);
		if (!binding)
			continue;

		GtkWidget *row = lds_terminal_shortcuts_make_editable_row(terminal, binding);
		switch (binding->group) {
		case LDS_TERMINAL_SHORTCUT_GROUP_WINDOW:
			adw_preferences_group_add(window_group, row);
			break;
		case LDS_TERMINAL_SHORTCUT_GROUP_EDITING:
			adw_preferences_group_add(editing_group, row);
			break;
		case LDS_TERMINAL_SHORTCUT_GROUP_VIEW:
			adw_preferences_group_add(view_group, row);
			break;
		case LDS_TERMINAL_SHORTCUT_GROUP_PANE:
			adw_preferences_group_add(pane_group, row);
			break;
		default:
			break;
		}
	}

	adw_preferences_page_add(page, window_group);
	adw_preferences_page_add(page, editing_group);
	adw_preferences_page_add(page, view_group);
	adw_preferences_page_add(page, pane_group);
	adw_preferences_dialog_add(dialog, page);

	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(terminal->window));
}

static GtkWidget *
lds_terminal_shortcuts_make_editable_row(LdsTerminal *terminal,
										 const LdsTerminalShortcutBinding *binding) {
	AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
								  binding && binding->title ? binding->title : "");

	GtkWidget *button = gtk_button_new();
	gtk_widget_add_css_class(button, "flat");
	gtk_widget_add_css_class(button, "monospace");
	adw_action_row_add_suffix(row, button);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

	LdsTerminalShortcutRowData *row_data = g_new0(LdsTerminalShortcutRowData, 1);
	row_data->terminal = terminal;
	row_data->binding = binding;
	row_data->button = button;
	g_object_set_data_full(G_OBJECT(button), "shortcut-row-data", row_data, g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(lds_terminal_on_shortcut_row_button_clicked),
					 row_data);
	lds_terminal_shortcuts_refresh_row(row_data);

	return GTK_WIDGET(row);
}

static void lds_terminal_shortcuts_refresh_row(LdsTerminalShortcutRowData *row_data) {
	if (!row_data || !row_data->button || !row_data->binding || !row_data->binding->getter)
		return;

	const char *accel = row_data->binding->getter();
	g_autofree char *label = lds_terminal_shortcuts_label_from_accel(accel);
	gtk_button_set_label(GTK_BUTTON(row_data->button), label ? label : "—");
}

static char *lds_terminal_shortcuts_label_from_accel(const char *accel) {
	if (!accel || !*accel)
		return g_strdup("—");

	g_autoptr(GtkShortcutTrigger) trigger = gtk_shortcut_trigger_parse_string(accel);
	if (trigger)
		return gtk_shortcut_trigger_to_label(trigger, gdk_display_get_default());

	guint key = 0;
	GdkModifierType mods = 0;
	if (gtk_accelerator_parse(accel, &key, &mods))
		return gtk_accelerator_get_label(key, mods);

	return g_strdup(accel);
}

static gboolean lds_terminal_shortcuts_capture_key_pressed(GtkEventControllerKey *controller,
														   guint keyval, guint keycode,
														   GdkModifierType state,
														   gpointer user_data) {
	(void)controller;
	(void)keycode;
	LdsTerminalShortcutCaptureData *data = user_data;
	if (!data || !data->dialog)
		return GDK_EVENT_PROPAGATE;

	if (keyval == GDK_KEY_Escape)
		return GDK_EVENT_PROPAGATE;

	if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R || keyval == GDK_KEY_Control_L ||
		keyval == GDK_KEY_Control_R || keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
		keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R || keyval == GDK_KEY_Meta_L ||
		keyval == GDK_KEY_Meta_R)
		return GDK_EVENT_STOP;

	GdkModifierType accel_mods = state & gtk_accelerator_get_default_mod_mask();
	g_free(data->captured_accel);
	data->captured_accel = gtk_accelerator_name(keyval, accel_mods);

	g_autofree char *label = lds_terminal_shortcuts_label_from_accel(data->captured_accel);
	gtk_label_set_text(GTK_LABEL(data->preview), label ? label : "—");
	adw_alert_dialog_set_response_enabled(data->dialog, "apply", TRUE);

	return GDK_EVENT_STOP;
}

static void lds_terminal_shortcuts_capture_response(AdwAlertDialog *dialog, const char *response,
													gpointer user_data) {
	(void)dialog;
	LdsTerminalShortcutCaptureData *data = user_data;
	if (!data || !data->row || !data->row->binding || !data->row->terminal)
		return;

	if (g_strcmp0(response, "clear") == 0) {
		data->row->binding->setter("");
	} else if (g_strcmp0(response, "apply") == 0 && data->captured_accel && *data->captured_accel) {
		const LdsTerminalShortcutBinding *conflict = NULL;
		if (lds_terminal_shortcuts_has_conflict(data->row->binding, data->captured_accel,
												&conflict)) {
			g_autofree gchar *msg = g_strdup_printf(
				"The shortcut is already assigned to \"%s\". Choose a different combination.",
				conflict->title);
			AdwAlertDialog *warn = ADW_ALERT_DIALOG(adw_alert_dialog_new("Shortcut conflict", msg));
			adw_alert_dialog_add_response(warn, "ok", "OK");
			adw_alert_dialog_set_default_response(warn, "ok");
			adw_alert_dialog_set_close_response(warn, "ok");
			adw_dialog_present(ADW_DIALOG(warn), GTK_WIDGET(data->row->terminal->window));
			return;
		}
		data->row->binding->setter(data->captured_accel);
	}

	if (data->row->terminal && data->row->terminal->parent && data->row->terminal->parent->app)
		lds_terminal_shortcuts_apply_to_app(data->row->terminal->parent->app);
	lds_terminal_menu_refresh_popover(data->row->terminal);
	lds_terminal_shortcuts_refresh_row(data->row);
}

static void lds_terminal_shortcuts_capture_data_free(gpointer user_data, GClosure *closure) {
	(void)closure;
	LdsTerminalShortcutCaptureData *data = user_data;
	if (!data)
		return;
	g_clear_pointer(&data->captured_accel, g_free);
	g_free(data);
}

static void lds_terminal_on_shortcut_row_button_clicked(GtkButton *button, gpointer user_data) {
	(void)button;
	LdsTerminalShortcutRowData *row_data = user_data;
	if (!row_data || !row_data->terminal || !row_data->terminal->window || !row_data->binding)
		return;

	AdwAlertDialog *dialog =
		ADW_ALERT_DIALOG(adw_alert_dialog_new("Set shortcut", "Press the new key combination"));
	adw_alert_dialog_add_response(dialog, "cancel", "Cancel");
	adw_alert_dialog_add_response(dialog, "clear", "Clear");
	adw_alert_dialog_add_response(dialog, "apply", "Apply");
	adw_alert_dialog_set_default_response(dialog, "apply");
	adw_alert_dialog_set_response_enabled(dialog, "apply", FALSE);

	g_autofree char *label = lds_terminal_shortcuts_label_from_accel(row_data->binding->getter());
	GtkWidget *preview = gtk_label_new(label ? label : "—");
	gtk_widget_add_css_class(preview, "title-4");
	adw_alert_dialog_set_extra_child(dialog, preview);

	LdsTerminalShortcutCaptureData *capture = g_new0(LdsTerminalShortcutCaptureData, 1);
	capture->row = row_data;
	capture->dialog = dialog;
	capture->preview = preview;

	GtkEventController *key = GTK_EVENT_CONTROLLER(gtk_event_controller_key_new());
	g_signal_connect(key, "key-pressed", G_CALLBACK(lds_terminal_shortcuts_capture_key_pressed),
					 capture);
	gtk_widget_add_controller(GTK_WIDGET(dialog), key);

	g_signal_connect_data(dialog, "response", G_CALLBACK(lds_terminal_shortcuts_capture_response),
						  capture, lds_terminal_shortcuts_capture_data_free, 0);
	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(row_data->terminal->window));
}
