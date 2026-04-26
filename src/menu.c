/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application menu actions and dialogs.
 */

/* Menu construction and callbacks. */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "menu.h"
#include "settings.h"
#include "vte.h"
#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"
#include "internal/prefs_shortcuts.h"
#include "internal/shortcuts_registry.h"

static void lds_terminal_action_new_window(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_new_tab(GSimpleAction *action, GVariant *parameter,
										gpointer user_data);
static void lds_terminal_action_close_tab(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data);
static void lds_terminal_action_rename_tab(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_close_window(GSimpleAction *action, GVariant *parameter,
											 gpointer user_data);
static void lds_terminal_action_copy(GSimpleAction *action, GVariant *parameter,
									 gpointer user_data);
static void lds_terminal_action_paste(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data);
static void lds_terminal_action_clear(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data);
static void lds_terminal_action_reset(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data);
static void lds_terminal_action_zoom_in(GSimpleAction *action, GVariant *parameter,
										gpointer user_data);
static void lds_terminal_action_zoom_out(GSimpleAction *action, GVariant *parameter,
										 gpointer user_data);
static void lds_terminal_action_zoom_reset(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_split_vertical(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data);
static void lds_terminal_action_close_pane(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_focus_next_pane(GSimpleAction *action, GVariant *parameter,
												gpointer user_data);
static void lds_terminal_action_about(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data);
static void lds_terminal_action_preferences(GSimpleAction *action, GVariant *parameter,
											gpointer user_data);
static void lds_terminal_action_shortcuts(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data);
static void lds_terminal_on_menu_button_active_notify(GObject *object, GParamSpec *pspec,
													  gpointer user_data);

static void lds_terminal_menu_setup_actions(LdsTerminal *terminal);
static GtkWidget *lds_terminal_menu_build_popover(void);
static GMenuModel *lds_terminal_menu_clone_model_with_runtime_accels(GMenuModel *model);
static void lds_terminal_menu_item_apply_runtime_accel(GMenuItem *item, const char *action_name);
static LdsTerminalTerm *lds_terminal_menu_get_current_term(LdsTerminal *terminal);
static gboolean lds_terminal_menu_term_has_selection(LdsTerminalTerm *term);
static gboolean lds_terminal_menu_term_clipboard_has_text_query(LdsTerminalTerm *term);
static void lds_terminal_menu_set_action_enabled_if_changed(GAction *action, gboolean enabled);
static void lds_terminal_menu_cache_actions(LdsTerminal *terminal);
static void lds_terminal_menu_sync_term_context_actions(LdsTerminalTerm *term,
														gboolean copy_enabled,
														gboolean paste_enabled,
														gboolean term_active);

#define LDS_MENU_EDIT_STATE_COPY (1u << 0)
#define LDS_MENU_EDIT_STATE_PASTE (1u << 1)
#define LDS_MENU_EDIT_STATE_CLEAR (1u << 2)
#define LDS_MENU_EDIT_STATE_RESET (1u << 3)
#define LDS_MENU_EDIT_STATE_RENAME_TAB (1u << 4)
#define LDS_MENU_EDIT_STATE_SPLIT_VERTICAL (1u << 5)
#define LDS_MENU_EDIT_STATE_CLOSE_PANE (1u << 6)
#define LDS_MENU_EDIT_STATE_FOCUS_NEXT_PANE (1u << 7)

/**
 * lds_terminal_menu_initialize:
 *
 * Initialize menu widgets and action wiring.
 *
 * The menu button is stored in @terminal->menu; ownership is transferred when
 * the window builder packs it into the header bar.
 */
void lds_terminal_menu_initialize(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;

	lds_terminal_menu_setup_actions(terminal);

	GtkWidget *menu_button = gtk_menu_button_new();
	GtkWidget *icon = gtk_image_new_from_icon_name("open-menu-symbolic");

	gtk_menu_button_set_child(GTK_MENU_BUTTON(menu_button), icon);
	gtk_widget_set_halign(menu_button, GTK_ALIGN_END);
	gtk_widget_set_valign(menu_button, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(menu_button, _("Menu"));
	gtk_widget_add_css_class(menu_button, "flat");
	terminal->menu = menu_button;
	lds_terminal_menu_refresh_popover(terminal);
	g_signal_connect(menu_button, "notify::active",
					 G_CALLBACK(lds_terminal_on_menu_button_active_notify), terminal);

	lds_terminal_menu_sync_edit_actions(terminal);
}

void lds_terminal_menu_refresh_popover(LdsTerminal *terminal) {
	if (!terminal || !terminal->menu || !GTK_IS_MENU_BUTTON(terminal->menu))
		return;

	GtkWidget *popover = lds_terminal_menu_build_popover();
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(terminal->menu), popover);
}

/**
 * lds_terminal_menu_update_accelerators:
 *
 * Update mnemonic behavior for the menu.
 */
void lds_terminal_menu_update_accelerators(LdsTerminal *terminal) {
	GtkSettings *settings = gtk_settings_get_default();
	if (!settings)
		return;

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-enable-mnemonics")) {
		g_object_set(settings, "gtk-enable-mnemonics", lds_terminal_settings_alt_enabled(), NULL);
	}

	/* Headerbar popover shows runtime accel labels derived from current settings. */
	lds_terminal_menu_refresh_popover(terminal);
}

void lds_terminal_menu_invalidate_clipboard_cache(LdsTerminal *terminal) {
	if (!terminal)
		return;

	terminal->menu_paste_cache_term = NULL;
	terminal->menu_paste_cache_valid = FALSE;
}

gboolean lds_terminal_menu_clipboard_has_text_cached(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term || !term->vte || term->closing)
		return FALSE;

	if (!terminal->menu_paste_cache_valid || terminal->menu_paste_cache_term != term) {
		terminal->menu_paste_enabled_cache = lds_terminal_menu_term_clipboard_has_text_query(term);
		terminal->menu_paste_cache_term = term;
		terminal->menu_paste_cache_valid = TRUE;
	}

	return terminal->menu_paste_enabled_cache;
}

void lds_terminal_menu_sync_edit_actions(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed || !terminal->window)
		return;

	if (!terminal->window_actions)
		return;
	lds_terminal_menu_cache_actions(terminal);

	LdsTerminalTerm *term = lds_terminal_menu_get_current_term(terminal);
	gboolean term_active = term && term->vte && !term->closing;
	gboolean copy_enabled = term_active && lds_terminal_menu_term_has_selection(term);
	gboolean has_split = term_active && lds_terminal_vte_has_split(term);
	gboolean split_enabled = term_active && !has_split;
	gboolean pane_actions_enabled = has_split;
	gboolean paste_enabled = FALSE;

	if (term_active)
		paste_enabled = lds_terminal_menu_clipboard_has_text_cached(terminal, term);

	guint next_state_bits = 0;
	if (copy_enabled)
		next_state_bits |= LDS_MENU_EDIT_STATE_COPY;
	if (paste_enabled)
		next_state_bits |= LDS_MENU_EDIT_STATE_PASTE;
	if (term_active)
		next_state_bits |= LDS_MENU_EDIT_STATE_CLEAR | LDS_MENU_EDIT_STATE_RESET;
	if (term_active)
		next_state_bits |= LDS_MENU_EDIT_STATE_RENAME_TAB;
	if (split_enabled)
		next_state_bits |= LDS_MENU_EDIT_STATE_SPLIT_VERTICAL;
	if (pane_actions_enabled)
		next_state_bits |= LDS_MENU_EDIT_STATE_CLOSE_PANE | LDS_MENU_EDIT_STATE_FOCUS_NEXT_PANE;

	if (terminal->menu_edit_state_valid && terminal->menu_edit_state_term == term &&
		terminal->menu_edit_state_bits == next_state_bits)
		return;

	terminal->menu_edit_state_term = term;
	terminal->menu_edit_state_bits = next_state_bits;
	terminal->menu_edit_state_valid = TRUE;

	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_copy, copy_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_paste, paste_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_clear, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_reset, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_rename_tab, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_zoom_in, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_zoom_out, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_zoom_reset, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_split_vertical, split_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_close_pane,
													pane_actions_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(terminal->action_focus_next_pane,
													pane_actions_enabled);

	if (term_active)
		lds_terminal_menu_sync_term_context_actions(term, copy_enabled, paste_enabled, term_active);
}

static LdsTerminalTerm *lds_terminal_menu_get_current_term(LdsTerminal *terminal) {
	return lds_terminal_get_current_term(terminal);
}

static gboolean lds_terminal_menu_term_has_selection(LdsTerminalTerm *term) {
	return lds_terminal_vte_active_has_selection(term);
}

static gboolean lds_terminal_menu_term_clipboard_has_text_query(LdsTerminalTerm *term) {
	return lds_terminal_vte_active_clipboard_has_text(term);
}

static void lds_terminal_menu_set_action_enabled_if_changed(GAction *action, gboolean enabled) {
	if (!action || !G_IS_SIMPLE_ACTION(action))
		return;

	if (g_action_get_enabled(action) == enabled)
		return;

	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

static void lds_terminal_menu_sync_term_context_actions(LdsTerminalTerm *term,
														gboolean copy_enabled,
														gboolean paste_enabled,
														gboolean term_active) {
	if (!term || !term->context_actions)
		return;

	GAction *copy = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "copy");
	GAction *paste = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "paste");
	GAction *clear = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "clear");
	GAction *reset = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "reset");

	lds_terminal_menu_set_action_enabled_if_changed(copy, copy_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(paste, paste_enabled);
	lds_terminal_menu_set_action_enabled_if_changed(clear, term_active);
	lds_terminal_menu_set_action_enabled_if_changed(reset, term_active);
}

GMenuModel *lds_terminal_menu_build_context_model(void) {
	/*
	 * Process-lifetime singleton: this model is immutable and shared by every
	 * terminal context popover. Keeping one ref for app lifetime avoids rebuild
	 * churn and is an intentional trade-off in this desktop process.
	 */
	static GMenuModel *model = NULL;
	if (model)
		return g_object_ref(model);

	GMenu *root = g_menu_new();

	GMenu *link_section = g_menu_new();
	{
		GMenuItem *open_link = g_menu_item_new(_("Open Link"), "term.open-link");
		g_menu_item_set_attribute(open_link, "hidden-when", "s", "action-disabled");
		g_menu_item_set_attribute_value(open_link, "accel", NULL);
		g_menu_append_item(link_section, open_link);
		g_object_unref(open_link);

		GMenuItem *copy_link = g_menu_item_new(_("Copy Link"), "term.copy-link");
		g_menu_item_set_attribute(copy_link, "hidden-when", "s", "action-disabled");
		g_menu_item_set_attribute_value(copy_link, "accel", NULL);
		g_menu_append_item(link_section, copy_link);
		g_object_unref(copy_link);
	}
	g_menu_append_section(root, NULL, G_MENU_MODEL(link_section));
	g_object_unref(link_section);

	GMenu *edit_section = g_menu_new();
	{

		/* Context menu actions are term.* because they operate on the pane that opened it. */
		GMenuItem *copy = g_menu_item_new(_("Copy"), "term.copy");
		g_menu_item_set_attribute_value(copy, "accel", NULL);
		g_menu_append_item(edit_section, copy);
		g_object_unref(copy);

		GMenuItem *paste = g_menu_item_new(_("Paste"), "term.paste");
		g_menu_item_set_attribute_value(paste, "accel", NULL);
		g_menu_append_item(edit_section, paste);
		g_object_unref(paste);
	}
	g_menu_append_section(root, NULL, G_MENU_MODEL(edit_section));
	g_object_unref(edit_section);

	GMenu *tab_section = g_menu_new();
	/* Window/tab lifecycle actions remain win.* and resolve through the active tab state. */
	{
		GMenuItem *item = g_menu_item_new(_("New Tab"), "win.new-tab");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(tab_section, item);
		g_object_unref(item);

		item = g_menu_item_new(_("Rename Tab"), "win.rename-tab");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(tab_section, item);
		g_object_unref(item);

		item = g_menu_item_new(_("Close Tab"), "win.close-tab");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(tab_section, item);
		g_object_unref(item);

		item = g_menu_item_new(_("Split Pane"), "win.split-vertical");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(tab_section, item);
		g_object_unref(item);

		item = g_menu_item_new(_("Close Pane"), "win.close-pane");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(tab_section, item);
		g_object_unref(item);
	}
	g_menu_append_section(root, NULL, G_MENU_MODEL(tab_section));
	g_object_unref(tab_section);

	GMenu *terminal_section = g_menu_new();
	/* Terminal operations in context menu stay term.* for the same pane-scoped semantics. */
	{
		GMenuItem *item = g_menu_item_new(_("Clear"), "term.clear");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(terminal_section, item);
		g_object_unref(item);

		item = g_menu_item_new(_("Reset"), "term.reset");
		g_menu_item_set_attribute_value(item, "accel", NULL);
		g_menu_append_item(terminal_section, item);
		g_object_unref(item);
	}
	g_menu_append_section(root, NULL, G_MENU_MODEL(terminal_section));
	g_object_unref(terminal_section);

	model = G_MENU_MODEL(root);
	return g_object_ref(model);
}

static void lds_terminal_action_new_window(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_spawn_window(user_data);
}

static void lds_terminal_action_new_tab(GSimpleAction *action, GVariant *parameter,
										gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_new_tab(user_data, NULL);
}

static void lds_terminal_action_close_tab(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_close_current_tab(user_data);
}

static void lds_terminal_action_rename_tab(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_rename_current_tab(user_data);
}

static void lds_terminal_action_close_window(GSimpleAction *action, GVariant *parameter,
											 gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_close_window(user_data);
}

static void lds_terminal_action_copy(GSimpleAction *action, GVariant *parameter,
									 gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_copy(user_data);
}

static void lds_terminal_action_paste(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_paste(user_data);
}

static void lds_terminal_action_clear(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_clear(user_data);
}

static void lds_terminal_action_reset(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_reset(user_data);
}

static void lds_terminal_action_zoom_in(GSimpleAction *action, GVariant *parameter,
										gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_zoom_in(user_data);
}

static void lds_terminal_action_zoom_out(GSimpleAction *action, GVariant *parameter,
										 gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_zoom_out(user_data);
}

static void lds_terminal_action_zoom_reset(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_zoom_reset(user_data);
}

static void lds_terminal_action_split_vertical(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_split_vertical(user_data);
}

static void lds_terminal_action_close_pane(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_close_pane(user_data);
}

static void lds_terminal_action_focus_next_pane(GSimpleAction *action, GVariant *parameter,
												gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_focus_next_pane(user_data);
}

static void lds_terminal_action_about(GSimpleAction *action, GVariant *parameter,
									  gpointer user_data) {
	(void)action;
	(void)parameter;
	LdsTerminal *terminal = user_data;

	AdwAboutDialog *dlg = ADW_ABOUT_DIALOG(adw_about_dialog_new());

	adw_about_dialog_set_application_name(dlg, LDS_TERMINAL_DISPLAY_NAME);
	adw_about_dialog_set_version(dlg, LDS_TERMINAL_VERSION);
	adw_about_dialog_set_application_icon(dlg, "lds-terminal");
	adw_about_dialog_set_developer_name(dlg, "Iuri Suzano");
	adw_about_dialog_set_comments(dlg, _("A disciplined GTK terminal emulator"));
	adw_about_dialog_set_copyright(dlg, "Copyright © 2026 Iuri Suzano");
	adw_about_dialog_set_license_type(dlg, GTK_LICENSE_MIT_X11);

	adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(terminal->window));
}

static void lds_terminal_action_preferences(GSimpleAction *action, GVariant *parameter,
											gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_prefs_show(user_data);
}

static void lds_terminal_action_shortcuts(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_shortcuts_show(user_data);
}

static void lds_terminal_on_menu_button_active_notify(GObject *object, GParamSpec *pspec,
													  gpointer user_data) {
	(void)object;
	(void)pspec;
	LdsTerminal *terminal = (LdsTerminal *)user_data;
	if (!terminal || terminal->destroyed)
		return;

	/* Recompute on open so Paste reflects external clipboard changes immediately. */
	if (GTK_IS_MENU_BUTTON(object) && gtk_menu_button_get_active(GTK_MENU_BUTTON(object))) {
		lds_terminal_menu_invalidate_clipboard_cache(terminal);
		terminal->menu_edit_state_valid = FALSE;
	}

	lds_terminal_menu_sync_edit_actions(terminal);
}

static void lds_terminal_menu_setup_actions(LdsTerminal *terminal) {
	static const GActionEntry entries[] = {
		{"new-window", lds_terminal_action_new_window, NULL, NULL, NULL, {0}},
		{"new-tab", lds_terminal_action_new_tab, NULL, NULL, NULL, {0}},
		{"rename-tab", lds_terminal_action_rename_tab, NULL, NULL, NULL, {0}},
		{"close-tab", lds_terminal_action_close_tab, NULL, NULL, NULL, {0}},
		{"close-window", lds_terminal_action_close_window, NULL, NULL, NULL, {0}},
		{"preferences", lds_terminal_action_preferences, NULL, NULL, NULL, {0}},
		{"shortcuts", lds_terminal_action_shortcuts, NULL, NULL, NULL, {0}},
		{"copy", lds_terminal_action_copy, NULL, NULL, NULL, {0}},
		{"paste", lds_terminal_action_paste, NULL, NULL, NULL, {0}},
		{"clear", lds_terminal_action_clear, NULL, NULL, NULL, {0}},
		{"reset", lds_terminal_action_reset, NULL, NULL, NULL, {0}},
		{"zoom-in", lds_terminal_action_zoom_in, NULL, NULL, NULL, {0}},
		{"zoom-out", lds_terminal_action_zoom_out, NULL, NULL, NULL, {0}},
		{"zoom-reset", lds_terminal_action_zoom_reset, NULL, NULL, NULL, {0}},
		{"split-vertical", lds_terminal_action_split_vertical, NULL, NULL, NULL, {0}},
		{"close-pane", lds_terminal_action_close_pane, NULL, NULL, NULL, {0}},
		{"focus-next-pane", lds_terminal_action_focus_next_pane, NULL, NULL, NULL, {0}},
		{"about", lds_terminal_action_about, NULL, NULL, NULL, {0}},
	};

	GSimpleActionGroup *group = g_simple_action_group_new();
	g_action_map_add_action_entries(G_ACTION_MAP(group), entries, G_N_ELEMENTS(entries), terminal);
	gtk_widget_insert_action_group(GTK_WIDGET(terminal->window), "win", G_ACTION_GROUP(group));
	terminal->window_actions = g_object_ref(group);
	g_object_unref(group);

	lds_terminal_menu_cache_actions(terminal);
}

static GtkWidget *lds_terminal_menu_build_popover(void) {
	GtkBuilder *builder = gtk_builder_new_from_resource("/bar/astware/lds-terminal/menu.ui");
	GMenuModel *base_model = G_MENU_MODEL(gtk_builder_get_object(builder, "terminal-menu"));
	GMenuModel *model = lds_terminal_menu_clone_model_with_runtime_accels(base_model);
	GtkWidget *popover = gtk_popover_menu_new_from_model(model);
	g_object_unref(model);
	g_object_unref(builder);
	return popover;
}

static GMenuModel *lds_terminal_menu_clone_model_with_runtime_accels(GMenuModel *model) {
	GMenu *out = g_menu_new();
	if (!model)
		return G_MENU_MODEL(out);

	gint n_items = g_menu_model_get_n_items(model);
	for (gint i = 0; i < n_items; i++) {
		GMenuItem *item = g_menu_item_new_from_model(model, i);

		g_autoptr(GVariant) action = g_menu_model_get_item_attribute_value(
			model, i, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING);
		if (action) {
			const char *action_name = g_variant_get_string(action, NULL);
			lds_terminal_menu_item_apply_runtime_accel(item, action_name);
		} else {
			g_menu_item_set_attribute_value(item, "accel", NULL);
		}

		GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		if (section) {
			GMenuModel *section_copy = lds_terminal_menu_clone_model_with_runtime_accels(section);
			g_menu_item_set_link(item, G_MENU_LINK_SECTION, section_copy);
			g_object_unref(section_copy);
			g_object_unref(section);
		}

		GMenuModel *submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
		if (submenu) {
			GMenuModel *submenu_copy = lds_terminal_menu_clone_model_with_runtime_accels(submenu);
			g_menu_item_set_link(item, G_MENU_LINK_SUBMENU, submenu_copy);
			g_object_unref(submenu_copy);
			g_object_unref(submenu);
		}

		g_menu_append_item(out, item);
		g_object_unref(item);
	}

	return G_MENU_MODEL(out);
}

static void lds_terminal_menu_item_apply_runtime_accel(GMenuItem *item, const char *action_name) {
	if (!item || !action_name || !*action_name)
		return;

	const LdsTerminalShortcutBinding *binding = lds_terminal_shortcuts_find_by_action(action_name);
	if (!binding || !binding->getter) {
		g_menu_item_set_attribute_value(item, "accel", NULL);
		return;
	}

	const char *accel = binding->getter();
	if (accel && *accel)
		g_menu_item_set_attribute(item, "accel", "s", accel);
	else
		g_menu_item_set_attribute_value(item, "accel", NULL);
}

static void lds_terminal_menu_cache_actions(LdsTerminal *terminal) {
	if (!terminal || !terminal->window_actions)
		return;

	if (terminal->menu_actions_cached_from != terminal->window_actions) {
		terminal->menu_actions_cached_from = terminal->window_actions;
		terminal->action_copy = NULL;
		terminal->action_paste = NULL;
		terminal->action_clear = NULL;
		terminal->action_reset = NULL;
		terminal->action_rename_tab = NULL;
		terminal->action_zoom_in = NULL;
		terminal->action_zoom_out = NULL;
		terminal->action_zoom_reset = NULL;
		terminal->action_split_vertical = NULL;
		terminal->action_close_pane = NULL;
		terminal->action_focus_next_pane = NULL;
	}

	if (terminal->action_copy && terminal->action_paste && terminal->action_clear &&
		terminal->action_reset && terminal->action_rename_tab && terminal->action_zoom_in &&
		terminal->action_zoom_out && terminal->action_zoom_reset &&
		terminal->action_split_vertical && terminal->action_close_pane &&
		terminal->action_focus_next_pane)
		return;

	terminal->action_copy =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "copy");
	terminal->action_paste =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "paste");
	terminal->action_clear =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "clear");
	terminal->action_reset =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "reset");
	terminal->action_rename_tab =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "rename-tab");
	terminal->action_zoom_in =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "zoom-in");
	terminal->action_zoom_out =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "zoom-out");
	terminal->action_zoom_reset =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "zoom-reset");
	terminal->action_split_vertical =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "split-vertical");
	terminal->action_close_pane =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "close-pane");
	terminal->action_focus_next_pane =
		g_action_map_lookup_action(G_ACTION_MAP(terminal->window_actions), "focus-next-pane");
}
