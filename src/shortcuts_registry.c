/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Shortcut registry metadata and action binding.
 */

#include "settings.h"
#include "internal/shortcuts_registry.h"

static const LdsTerminalShortcutBinding lds_terminal_shortcut_bindings[] = {
	{"New Window", "win.new-window", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_new_window_accel, lds_terminal_settings_set_new_window_accel},
	{"New Tab", "win.new-tab", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_new_tab_accel, lds_terminal_settings_set_new_tab_accel},
	{"Close Tab", "win.close-tab", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_close_tab_accel, lds_terminal_settings_set_close_tab_accel},
	{"Rename Tab", "win.rename-tab", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_rename_tab_accel, lds_terminal_settings_set_rename_tab_accel},
	{"Close Window", "win.close-window", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_close_window_accel, lds_terminal_settings_set_close_window_accel},
	{"Preferences", "win.preferences", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_preferences_accel, lds_terminal_settings_set_preferences_accel},
	{"Shortcuts", "win.shortcuts", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW,
	 lds_terminal_settings_shortcuts_accel, lds_terminal_settings_set_shortcuts_accel},
	{"About", "win.about", LDS_TERMINAL_SHORTCUT_GROUP_WINDOW, lds_terminal_settings_about_accel,
	 lds_terminal_settings_set_about_accel},
	{"Copy", "win.copy", LDS_TERMINAL_SHORTCUT_GROUP_EDITING, lds_terminal_settings_copy_accel,
	 lds_terminal_settings_set_copy_accel},
	{"Paste", "win.paste", LDS_TERMINAL_SHORTCUT_GROUP_EDITING, lds_terminal_settings_paste_accel,
	 lds_terminal_settings_set_paste_accel},
	{"Clear", "win.clear", LDS_TERMINAL_SHORTCUT_GROUP_EDITING, lds_terminal_settings_clear_accel,
	 lds_terminal_settings_set_clear_accel},
	{"Reset", "win.reset", LDS_TERMINAL_SHORTCUT_GROUP_EDITING, lds_terminal_settings_reset_accel,
	 lds_terminal_settings_set_reset_accel},
	{"Zoom In", "win.zoom-in", LDS_TERMINAL_SHORTCUT_GROUP_VIEW,
	 lds_terminal_settings_zoom_in_accel, lds_terminal_settings_set_zoom_in_accel},
	{"Zoom Out", "win.zoom-out", LDS_TERMINAL_SHORTCUT_GROUP_VIEW,
	 lds_terminal_settings_zoom_out_accel, lds_terminal_settings_set_zoom_out_accel},
	{"Zoom Reset", "win.zoom-reset", LDS_TERMINAL_SHORTCUT_GROUP_VIEW,
	 lds_terminal_settings_zoom_reset_accel, lds_terminal_settings_set_zoom_reset_accel},
	{"Split Pane", "win.split-vertical", LDS_TERMINAL_SHORTCUT_GROUP_PANE,
	 lds_terminal_settings_split_vertical_accel, lds_terminal_settings_set_split_vertical_accel},
	{"Close Pane", "win.close-pane", LDS_TERMINAL_SHORTCUT_GROUP_PANE,
	 lds_terminal_settings_close_pane_accel, lds_terminal_settings_set_close_pane_accel},
	{"Focus Next Pane", "win.focus-next-pane", LDS_TERMINAL_SHORTCUT_GROUP_PANE,
	 lds_terminal_settings_focus_next_pane_accel, lds_terminal_settings_set_focus_next_pane_accel},
};

guint lds_terminal_shortcuts_count(void) {
	return G_N_ELEMENTS(lds_terminal_shortcut_bindings);
}

const LdsTerminalShortcutBinding *lds_terminal_shortcuts_at(guint index) {
	if (index >= G_N_ELEMENTS(lds_terminal_shortcut_bindings))
		return NULL;
	return &lds_terminal_shortcut_bindings[index];
}

const LdsTerminalShortcutBinding *lds_terminal_shortcuts_find_by_action(const char *action_name) {
	if (!action_name || !*action_name)
		return NULL;

	for (guint i = 0; i < G_N_ELEMENTS(lds_terminal_shortcut_bindings); i++) {
		const LdsTerminalShortcutBinding *binding = &lds_terminal_shortcut_bindings[i];
		if (g_strcmp0(binding->action_name, action_name) == 0)
			return binding;
	}

	return NULL;
}

const char *lds_terminal_shortcuts_accel_or_null(const char *accel) {
	return (accel && *accel) ? accel : NULL;
}

gchar *lds_terminal_shortcuts_normalize_accel(const char *accel) {
	if (!accel || !*accel)
		return g_strdup("");

	guint key = 0;
	GdkModifierType mods = 0;
	if (!gtk_accelerator_parse(accel, &key, &mods) || key == 0)
		return g_strdup(accel);

	return gtk_accelerator_name(key, mods & gtk_accelerator_get_default_mod_mask());
}

gboolean lds_terminal_shortcuts_has_conflict(const LdsTerminalShortcutBinding *self,
											 const char *accel,
											 const LdsTerminalShortcutBinding **hit) {
	if (hit)
		*hit = NULL;

	g_autofree gchar *norm_accel = lds_terminal_shortcuts_normalize_accel(accel);
	if (!norm_accel || !*norm_accel)
		return FALSE;

	for (guint i = 0; i < G_N_ELEMENTS(lds_terminal_shortcut_bindings); i++) {
		const LdsTerminalShortcutBinding *other = &lds_terminal_shortcut_bindings[i];
		if (other == self || !other->getter)
			continue;
		g_autofree gchar *norm_other = lds_terminal_shortcuts_normalize_accel(other->getter());
		if (norm_other && g_strcmp0(norm_other, norm_accel) == 0) {
			if (hit)
				*hit = other;
			return TRUE;
		}
	}

	return FALSE;
}

void lds_terminal_shortcuts_apply_to_app(GtkApplication *app) {
	if (!app)
		return;

	for (guint i = 0; i < G_N_ELEMENTS(lds_terminal_shortcut_bindings); i++) {
		const LdsTerminalShortcutBinding *binding = &lds_terminal_shortcut_bindings[i];
		const char *accel = binding->getter ? binding->getter() : NULL;
		const char *accels[] = {lds_terminal_shortcuts_accel_or_null(accel), NULL};
		gtk_application_set_accels_for_action(app, binding->action_name, accels);
	}
}
