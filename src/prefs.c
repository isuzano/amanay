/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Preferences dialog construction and binding.
 */

/* Preferences and shortcuts dialogs. */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "settings.h"
#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"

typedef struct {
	LdsTerminal *terminal;
	GtkWidget *entry;
} LdsTerminalFontDialogData;

typedef struct {
	LdsTerminal *terminal;
	GtkEntry *font_entry;
	GtkColorDialogButton *font_color_button;
	GtkColorDialogButton *background_color_button;
	AdwSwitchRow *sync_prompt_row;
	AdwSpinRow *scroll_row;
	AdwComboRow *cursor_row;
	AdwSwitchRow *cursor_blink_row;
	AdwComboRow *theme_row;
	AdwSwitchRow *confirm_close_row;
	AdwSwitchRow *alt_mnemonics_row;
	AdwComboRow *tab_position_row;
	AdwSwitchRow *hide_menu_row;
	AdwSwitchRow *strict_determinism_row;
} LdsTerminalPrefsWidgets;

static guint lds_terminal_prefs_cursor_shape_to_index(gint shape);
static gint lds_terminal_prefs_index_to_cursor_shape(guint selected);
static guint lds_terminal_prefs_theme_mode_to_index(gint mode);
static gint lds_terminal_prefs_index_to_theme_mode(guint selected);
static void lds_terminal_on_font_entry_changed(GtkEditable *editable, LdsTerminal *terminal);
static void lds_terminal_on_font_entry_activate(GtkEntry *entry, LdsTerminal *terminal);
static void lds_terminal_on_font_entry_focus(GObject *object, GParamSpec *pspec,
											 gpointer user_data);
static void lds_terminal_on_font_button_clicked(GtkButton *button, gpointer user_data);
static void lds_terminal_on_font_dialog_done(GObject *object, GAsyncResult *result,
											 gpointer user_data);
static void lds_terminal_on_font_color_changed(GtkColorDialogButton *button, GParamSpec *pspec,
											   gpointer user_data);
static void lds_terminal_on_background_color_changed(GtkColorDialogButton *button,
													 GParamSpec *pspec, gpointer user_data);
static void lds_terminal_on_sync_prompt_colors_changed(GObject *object, GParamSpec *pspec,
													   gpointer user_data);
static void lds_terminal_on_scrollback_changed(GObject *object, GParamSpec *pspec,
											   gpointer user_data);
static void lds_terminal_on_cursor_shape_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data);
static void lds_terminal_on_theme_mode_changed(GObject *object, GParamSpec *pspec,
											   gpointer user_data);
static void lds_terminal_on_confirm_close_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data);
static void lds_terminal_on_alt_mnemonics_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data);
static void lds_terminal_on_tab_position_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data);
static void lds_terminal_on_cursor_blink_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data);
static void lds_terminal_on_hide_menu_bar_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data);
static void lds_terminal_on_strict_determinism_changed(GObject *object, GParamSpec *pspec,
													   gpointer user_data);
static void lds_terminal_on_reset_activated(AdwActionRow *row, gpointer user_data);
static void lds_terminal_on_reset_response(AdwAlertDialog *dialog, const char *response,
										   gpointer user_data);
static void lds_terminal_prefs_sync_from_settings(gpointer user_data);

static guint lds_terminal_prefs_cursor_shape_to_index(gint shape) {
	if (shape == VTE_CURSOR_SHAPE_IBEAM)
		return 1;
	if (shape == VTE_CURSOR_SHAPE_UNDERLINE)
		return 2;
	return 0;
}

static gint lds_terminal_prefs_index_to_cursor_shape(guint selected) {
	if (selected == 1)
		return VTE_CURSOR_SHAPE_IBEAM;
	if (selected == 2)
		return VTE_CURSOR_SHAPE_UNDERLINE;
	return VTE_CURSOR_SHAPE_BLOCK;
}

static guint lds_terminal_prefs_theme_mode_to_index(gint mode) {
	guint selected = (guint)mode;
	return selected <= 2 ? selected : 0;
}

static gint lds_terminal_prefs_index_to_theme_mode(guint selected) {
	if (selected == 1)
		return LDS_TERMINAL_THEME_LIGHT;
	if (selected == 2)
		return LDS_TERMINAL_THEME_DARK;
	return LDS_TERMINAL_THEME_SYSTEM;
}

void lds_terminal_prefs_show(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;

	AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());
	adw_preferences_dialog_set_search_enabled(dialog, TRUE);
	adw_dialog_set_presentation_mode(ADW_DIALOG(dialog), ADW_DIALOG_FLOATING);

	AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
	adw_preferences_page_set_title(page, _("General"));

	AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(group, _("Terminal"));

	AdwActionRow *font_row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), _("Font"));

	GtkWidget *font_entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(font_entry), lds_terminal_settings_font_name());
	gtk_widget_set_hexpand(font_entry, TRUE);
	g_signal_connect(font_entry, "activate", G_CALLBACK(lds_terminal_on_font_entry_activate),
					 terminal);
	g_signal_connect(font_entry, "notify::has-focus", G_CALLBACK(lds_terminal_on_font_entry_focus),
					 terminal);

	GtkWidget *font_button = gtk_button_new_with_label(_("Choose"));
	gtk_widget_add_css_class(font_button, "flat");
	g_object_set_data(G_OBJECT(font_button), "font-entry", font_entry);
	g_signal_connect(font_button, "clicked", G_CALLBACK(lds_terminal_on_font_button_clicked),
					 terminal);

	adw_action_row_add_suffix(font_row, font_entry);
	adw_action_row_add_suffix(font_row, font_button);

	AdwActionRow *font_color_row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_color_row), _("Font color"));
	GtkColorDialog *font_color_dialog = gtk_color_dialog_new();
	gtk_color_dialog_set_title(font_color_dialog, _("Pick a Color"));
	gtk_color_dialog_set_modal(font_color_dialog, FALSE);
	gtk_color_dialog_set_with_alpha(font_color_dialog, TRUE);
	GtkWidget *font_color_button = gtk_color_dialog_button_new(font_color_dialog);
	gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(font_color_button),
									 lds_terminal_settings_font_color());
	g_signal_connect(font_color_button, "notify::rgba",
					 G_CALLBACK(lds_terminal_on_font_color_changed), terminal);
	adw_action_row_add_suffix(font_color_row, font_color_button);

	AdwActionRow *background_color_row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(background_color_row), _("Background color"));
	GtkColorDialog *background_color_dialog = gtk_color_dialog_new();
	gtk_color_dialog_set_title(background_color_dialog, _("Pick a Color"));
	gtk_color_dialog_set_modal(background_color_dialog, FALSE);
	gtk_color_dialog_set_with_alpha(background_color_dialog, TRUE);
	GtkWidget *background_color_button = gtk_color_dialog_button_new(background_color_dialog);
	gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(background_color_button),
									 lds_terminal_settings_background_color());
	g_signal_connect(background_color_button, "notify::rgba",
					 G_CALLBACK(lds_terminal_on_background_color_changed), terminal);
	adw_action_row_add_suffix(background_color_row, background_color_button);

	AdwSwitchRow *sync_prompt_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sync_prompt_row),
								  _("Sync prompt text with font color"));
	adw_switch_row_set_active(sync_prompt_row, lds_terminal_settings_sync_prompt_colors());
	g_signal_connect(sync_prompt_row, "notify::active",
					 G_CALLBACK(lds_terminal_on_sync_prompt_colors_changed), terminal);

	AdwSpinRow *scroll_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1000, 50000, 100));
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(scroll_row), _("Scrollback lines"));
	adw_spin_row_set_value(scroll_row, lds_terminal_settings_scrollback());
	g_signal_connect(scroll_row, "notify::value", G_CALLBACK(lds_terminal_on_scrollback_changed),
					 terminal);

	GtkStringList *cursor_model = gtk_string_list_new(NULL);
	gtk_string_list_append(cursor_model, _("Block"));
	gtk_string_list_append(cursor_model, _("I-Beam"));
	gtk_string_list_append(cursor_model, _("Underline"));

	AdwComboRow *cursor_row = ADW_COMBO_ROW(adw_combo_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cursor_row), _("Cursor shape"));
	adw_combo_row_set_model(cursor_row, G_LIST_MODEL(cursor_model));
	g_object_unref(cursor_model);

	adw_combo_row_set_selected(cursor_row,
							   lds_terminal_prefs_cursor_shape_to_index(
								   lds_terminal_settings_cursor_shape()));
	g_signal_connect(cursor_row, "notify::selected",
					 G_CALLBACK(lds_terminal_on_cursor_shape_changed), terminal);

	GtkStringList *theme_model = gtk_string_list_new(NULL);
	gtk_string_list_append(theme_model, _("System"));
	gtk_string_list_append(theme_model, _("Light"));
	gtk_string_list_append(theme_model, _("Dark"));
	AdwComboRow *theme_row = ADW_COMBO_ROW(adw_combo_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme_row), _("Theme"));
	adw_combo_row_set_model(theme_row, G_LIST_MODEL(theme_model));
	g_object_unref(theme_model);
	adw_combo_row_set_selected(theme_row, lds_terminal_prefs_theme_mode_to_index(
											 lds_terminal_settings_theme_mode()));
	g_signal_connect(theme_row, "notify::selected", G_CALLBACK(lds_terminal_on_theme_mode_changed),
					 terminal);

	AdwPreferencesGroup *reset_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(reset_group, _("Reset"));
	AdwActionRow *reset_row = ADW_ACTION_ROW(adw_action_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(reset_row), _("Reset to Defaults"));
	adw_action_row_set_subtitle(reset_row, _("Restore all preferences to default values"));
	gtk_widget_add_css_class(GTK_WIDGET(reset_row), "destructive-action");
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(reset_row), TRUE);
	gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(reset_row), FALSE);
	g_signal_connect(reset_row, "activated", G_CALLBACK(lds_terminal_on_reset_activated), NULL);

	adw_preferences_group_add(group, GTK_WIDGET(font_row));
	adw_preferences_group_add(group, GTK_WIDGET(font_color_row));
	adw_preferences_group_add(group, GTK_WIDGET(background_color_row));
	adw_preferences_group_add(group, GTK_WIDGET(sync_prompt_row));
	adw_preferences_group_add(group, GTK_WIDGET(scroll_row));
	adw_preferences_group_add(group, GTK_WIDGET(cursor_row));

	AdwSwitchRow *cursor_blink_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cursor_blink_row), _("Cursor blink"));
	adw_switch_row_set_active(cursor_blink_row, lds_terminal_settings_cursor_blink());
	g_signal_connect(cursor_blink_row, "notify::active",
					 G_CALLBACK(lds_terminal_on_cursor_blink_changed), terminal);
	adw_preferences_group_add(group, GTK_WIDGET(cursor_blink_row));
	adw_preferences_group_add(group, GTK_WIDGET(theme_row));

	AdwPreferencesGroup *ui_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
	adw_preferences_group_set_title(ui_group, _("Interface"));

	AdwSwitchRow *confirm_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(confirm_row),
								  _("Confirm close with multiple tabs"));
	adw_switch_row_set_active(confirm_row, lds_terminal_settings_confirm_close_enabled());
	g_signal_connect(confirm_row, "notify::active",
					 G_CALLBACK(lds_terminal_on_confirm_close_changed), terminal);
	adw_preferences_group_add(ui_group, GTK_WIDGET(confirm_row));

	AdwSwitchRow *alt_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(alt_row), _("Alt mnemonics"));
	adw_switch_row_set_active(alt_row, lds_terminal_settings_alt_enabled());
	g_signal_connect(alt_row, "notify::active", G_CALLBACK(lds_terminal_on_alt_mnemonics_changed),
					 terminal);
	adw_preferences_group_add(ui_group, GTK_WIDGET(alt_row));

	GtkStringList *tab_model = gtk_string_list_new(NULL);
	gtk_string_list_append(tab_model, _("Top"));
	gtk_string_list_append(tab_model, _("Bottom"));
	AdwComboRow *tab_pos_row = ADW_COMBO_ROW(adw_combo_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tab_pos_row), _("Tab bar position"));
	adw_combo_row_set_model(tab_pos_row, G_LIST_MODEL(tab_model));
	g_object_unref(tab_model);
	guint tab_selected = (lds_terminal_settings_tab_position() == GTK_POS_BOTTOM) ? 1 : 0;
	adw_combo_row_set_selected(tab_pos_row, tab_selected);
	g_signal_connect(tab_pos_row, "notify::selected",
					 G_CALLBACK(lds_terminal_on_tab_position_changed), terminal);
	adw_preferences_group_add(ui_group, GTK_WIDGET(tab_pos_row));

	AdwSwitchRow *hide_menu_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(hide_menu_row), _("Hide menu bar"));
	adw_switch_row_set_active(hide_menu_row, lds_terminal_settings_hide_menu_bar());
	g_signal_connect(hide_menu_row, "notify::active",
					 G_CALLBACK(lds_terminal_on_hide_menu_bar_changed), terminal);
	adw_preferences_group_add(ui_group, GTK_WIDGET(hide_menu_row));

	AdwSwitchRow *strict_determinism_row = ADW_SWITCH_ROW(adw_switch_row_new());
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(strict_determinism_row),
								  _("Strict Determinism"));
	adw_action_row_set_subtitle(
		ADW_ACTION_ROW(strict_determinism_row),
		_("Disable animations/effects and prioritize predictable drag/detach behavior"));
	adw_switch_row_set_active(strict_determinism_row, lds_terminal_settings_strict_determinism());
	g_signal_connect(strict_determinism_row, "notify::active",
					 G_CALLBACK(lds_terminal_on_strict_determinism_changed), terminal);
	adw_preferences_group_add(ui_group, GTK_WIDGET(strict_determinism_row));
	adw_preferences_group_add(reset_group, GTK_WIDGET(reset_row));

	adw_preferences_page_add(page, group);
	adw_preferences_page_add(page, ui_group);
	adw_preferences_page_add(page, reset_group);
	adw_preferences_dialog_add(dialog, page);

	LdsTerminalPrefsWidgets *prefs = g_new0(LdsTerminalPrefsWidgets, 1);
	prefs->terminal = terminal;
	prefs->font_entry = GTK_ENTRY(font_entry);
	prefs->font_color_button = GTK_COLOR_DIALOG_BUTTON(font_color_button);
	prefs->background_color_button = GTK_COLOR_DIALOG_BUTTON(background_color_button);
	prefs->sync_prompt_row = sync_prompt_row;
	prefs->scroll_row = scroll_row;
	prefs->cursor_row = cursor_row;
	prefs->cursor_blink_row = cursor_blink_row;
	prefs->theme_row = theme_row;
	prefs->confirm_close_row = confirm_row;
	prefs->alt_mnemonics_row = alt_row;
	prefs->tab_position_row = tab_pos_row;
	prefs->hide_menu_row = hide_menu_row;
	prefs->strict_determinism_row = strict_determinism_row;
	g_object_set_data_full(G_OBJECT(dialog), "prefs-widgets", prefs, g_free);
	g_object_set_data_full(G_OBJECT(dialog), "font-color-dialog", g_object_ref(font_color_dialog),
						   g_object_unref);
	g_object_set_data_full(G_OBJECT(dialog), "background-color-dialog",
						   g_object_ref(background_color_dialog), g_object_unref);
	g_object_set_data(G_OBJECT(reset_row), "prefs-widgets", prefs);

	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(terminal->window));
}

static void lds_terminal_on_font_entry_changed(GtkEditable *editable, LdsTerminal *terminal) {
	if (!terminal)
		return;

	const char *text = gtk_editable_get_text(editable);
	lds_terminal_settings_set_font_name(text);
	/* Font changes are per-window/VTE state, so apply only to the active terminal. */
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_font_entry_activate(GtkEntry *entry, LdsTerminal *terminal) {
	if (!terminal)
		return;

	lds_terminal_on_font_entry_changed(GTK_EDITABLE(entry), terminal);
}

static void lds_terminal_on_font_entry_focus(GObject *object, GParamSpec *pspec,
											 gpointer user_data) {
	(void)pspec;
	GtkWidget *entry = GTK_WIDGET(object);
	if (gtk_widget_has_focus(entry))
		return;

	lds_terminal_on_font_entry_changed(GTK_EDITABLE(entry), user_data);
}

static void lds_terminal_on_font_button_clicked(GtkButton *button, gpointer user_data) {
	LdsTerminal *terminal = user_data;
	if (!terminal || !terminal->window)
		return;

	GtkFontDialog *dialog = gtk_font_dialog_new();
	const char *current = lds_terminal_settings_font_name();
	PangoFontDescription *desc = NULL;
	if (current && *current)
		desc = pango_font_description_from_string(current);

	LdsTerminalFontDialogData *data = g_new0(LdsTerminalFontDialogData, 1);
	data->terminal = terminal;
	data->entry = g_object_get_data(G_OBJECT(button), "font-entry");

	gtk_font_dialog_choose_font(dialog, GTK_WINDOW(terminal->window), desc, NULL,
								lds_terminal_on_font_dialog_done, data);

	if (desc)
		pango_font_description_free(desc);
}

static void lds_terminal_on_font_dialog_done(GObject *object, GAsyncResult *result,
											 gpointer user_data) {
	GtkFontDialog *dialog = GTK_FONT_DIALOG(object);
	LdsTerminalFontDialogData *data = user_data;
	g_autoptr(GError) error = NULL;

	PangoFontDescription *desc = gtk_font_dialog_choose_font_finish(dialog, result, &error);
	if (error || !desc) {
		g_clear_pointer(&data, g_free);
		return;
	}

	g_autofree gchar *text = pango_font_description_to_string(desc);
	pango_font_description_free(desc);

	if (data->entry)
		gtk_editable_set_text(GTK_EDITABLE(data->entry), text);

	if (data->terminal) {
		lds_terminal_settings_set_font_name(text);
		lds_terminal_settings_apply_current(data->terminal);
	}

	g_free(data);
}

static void lds_terminal_on_font_color_changed(GtkColorDialogButton *button, GParamSpec *pspec,
											   gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	const GdkRGBA *color = gtk_color_dialog_button_get_rgba(button);
	if (!color)
		return;

	lds_terminal_settings_set_font_color(color);
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_background_color_changed(GtkColorDialogButton *button,
													 GParamSpec *pspec, gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	const GdkRGBA *color = gtk_color_dialog_button_get_rgba(button);
	if (!color)
		return;

	lds_terminal_settings_set_background_color(color);
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_sync_prompt_colors_changed(GObject *object, GParamSpec *pspec,
													   gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_sync_prompt_colors(adw_switch_row_get_active(row));
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_scrollback_changed(GObject *object, GParamSpec *pspec,
											   gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSpinRow *row = ADW_SPIN_ROW(object);
	lds_terminal_settings_set_scrollback((guint)adw_spin_row_get_value(row));
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_cursor_shape_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwComboRow *row = ADW_COMBO_ROW(object);
	lds_terminal_settings_set_cursor_shape(
		lds_terminal_prefs_index_to_cursor_shape(adw_combo_row_get_selected(row)));
	/* Cursor settings affect terminal widgets, so refresh only this terminal. */
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_theme_mode_changed(GObject *object, GParamSpec *pspec,
											   gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwComboRow *row = ADW_COMBO_ROW(object);
	lds_terminal_settings_set_theme_mode(
		lds_terminal_prefs_index_to_theme_mode(adw_combo_row_get_selected(row)));
	/* Theme is global UI state, so propagate to all open windows. */
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_confirm_close_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_confirm_close(adw_switch_row_get_active(row));
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_alt_mnemonics_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_disable_alt(!adw_switch_row_get_active(row));
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_tab_position_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwComboRow *row = ADW_COMBO_ROW(object);
	guint selected = adw_combo_row_get_selected(row);
	gint position = (selected == 1) ? GTK_POS_BOTTOM : GTK_POS_TOP;
	lds_terminal_settings_set_tab_position(position);
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_cursor_blink_changed(GObject *object, GParamSpec *pspec,
												 gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_cursor_blink(adw_switch_row_get_active(row));
	lds_terminal_settings_apply_current(terminal);
}

static void lds_terminal_on_hide_menu_bar_changed(GObject *object, GParamSpec *pspec,
												  gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_hide_menu_bar(adw_switch_row_get_active(row));
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_strict_determinism_changed(GObject *object, GParamSpec *pspec,
													   gpointer user_data) {
	(void)pspec;
	LdsTerminal *terminal = user_data;
	if (!terminal)
		return;

	AdwSwitchRow *row = ADW_SWITCH_ROW(object);
	lds_terminal_settings_set_strict_determinism(adw_switch_row_get_active(row));
	lds_terminal_settings_apply_to_all(terminal);
}

static void lds_terminal_on_reset_activated(AdwActionRow *row, gpointer user_data) {
	(void)user_data;
	LdsTerminalPrefsWidgets *prefs = g_object_get_data(G_OBJECT(row), "prefs-widgets");
	if (!prefs || !prefs->terminal)
		return;

	AdwAlertDialog *dialog =
		ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Reset all preferences?"),
											 _("This cannot be undone.")));
	adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
	adw_alert_dialog_add_response(dialog, "reset", _("Reset"));
	adw_alert_dialog_set_response_appearance(dialog, "reset", ADW_RESPONSE_DESTRUCTIVE);
	adw_alert_dialog_set_default_response(dialog, "cancel");
	adw_alert_dialog_set_close_response(dialog, "cancel");
	g_signal_connect(dialog, "response", G_CALLBACK(lds_terminal_on_reset_response), prefs);
	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(prefs->terminal->window));
}

static void lds_terminal_on_reset_response(AdwAlertDialog *dialog, const char *response,
										   gpointer user_data) {
	(void)dialog;
	if (g_strcmp0(response, "reset") != 0)
		return;

	LdsTerminalPrefsWidgets *prefs = user_data;
	if (!prefs || !prefs->terminal)
		return;

	lds_terminal_settings_reset_defaults();
	lds_terminal_settings_apply_to_all(prefs->terminal);
	lds_terminal_prefs_sync_from_settings(prefs);
}

static void lds_terminal_prefs_sync_from_settings(gpointer user_data) {
	LdsTerminalPrefsWidgets *prefs = user_data;
	if (!prefs)
		return;

	gtk_editable_set_text(GTK_EDITABLE(prefs->font_entry), lds_terminal_settings_font_name());
	gtk_color_dialog_button_set_rgba(prefs->font_color_button, lds_terminal_settings_font_color());
	gtk_color_dialog_button_set_rgba(prefs->background_color_button,
									 lds_terminal_settings_background_color());
	adw_switch_row_set_active(prefs->sync_prompt_row, lds_terminal_settings_sync_prompt_colors());
	adw_spin_row_set_value(prefs->scroll_row, lds_terminal_settings_scrollback());

	adw_combo_row_set_selected(prefs->cursor_row,
							   lds_terminal_prefs_cursor_shape_to_index(
								   lds_terminal_settings_cursor_shape()));

	adw_combo_row_set_selected(prefs->theme_row,
							   lds_terminal_prefs_theme_mode_to_index(
								   lds_terminal_settings_theme_mode()));
	adw_switch_row_set_active(prefs->cursor_blink_row, lds_terminal_settings_cursor_blink());
	adw_switch_row_set_active(prefs->confirm_close_row,
							  lds_terminal_settings_confirm_close_enabled());
	adw_switch_row_set_active(prefs->alt_mnemonics_row, lds_terminal_settings_alt_enabled());
	adw_combo_row_set_selected(prefs->tab_position_row,
							   lds_terminal_settings_tab_position() == GTK_POS_BOTTOM ? 1 : 0);
	adw_switch_row_set_active(prefs->hide_menu_row, lds_terminal_settings_hide_menu_bar());
	adw_switch_row_set_active(prefs->strict_determinism_row,
							  lds_terminal_settings_strict_determinism());
}
