/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Runtime settings and preference binding.
 */

/*
 * Settings storage and application.
 *
 * Threading model: main-thread only. The singleton is mutable and shared
 * process-wide; settings_lock is used only to serialize one-time bootstrap and
 * reload toggles, not to provide full concurrent access safety.
 */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "settings.h"
#include "menu.h"
#include "tabs.h"
#include "vte.h"

#include "internal/lds_terminal_settings_internal.h"
#include "internal/lds_terminal_internal.h"
#include "internal/shortcuts_registry.h"

static GSettings *settings = NULL;
static gboolean settings_loaded = FALSE;
static gboolean settings_has_sync_prompt_colors = FALSE;
static gboolean settings_has_font_color = FALSE;
static gboolean settings_has_background_color = FALSE;
static gboolean settings_has_theme_mode = FALSE;
static gboolean settings_has_hide_menu_bar = FALSE;
static gboolean settings_has_strict_determinism = FALSE;
static gboolean settings_has_confirm_running_process = FALSE;
static GMutex settings_lock;
static const gint lds_terminal_settings_min_window_size = 100;
static void lds_terminal_settings_load_from_gsettings(void);
static void lds_terminal_settings_apply_style(const LdsTerminalSettings *s);
static void lds_terminal_settings_apply_determinism(const LdsTerminalSettings *s);
static void lds_terminal_settings_apply_builtin_defaults(void);
static void lds_terminal_settings_clear_owned_strings(void);
static GSettingsSchema *lds_terminal_settings_try_schema_dir(const gchar *dir,
															 GSettingsSchemaSource *parent);
static gchar *lds_terminal_settings_exe_dir(void);
static gchar *lds_terminal_settings_rgba_to_string(const GdkRGBA *color);
static gboolean lds_terminal_settings_rgba_from_string(const char *value, GdkRGBA *out);
static gboolean lds_terminal_settings_schema_is_compatible(GSettingsSchema *schema);
static void lds_terminal_settings_set_accel_string(gchar **slot, const char *accel);

static gboolean lds_terminal_settings_schema_is_compatible(GSettingsSchema *schema) {
	if (!schema)
		return FALSE;

	/*
	 * Keep compatibility gate limited to essential keys. Optional keys are
	 * detected later via settings_has_* and handled with fallback defaults.
	 */
	return g_settings_schema_has_key(schema, "font-name") &&
		   g_settings_schema_has_key(schema, "scrollback") &&
		   g_settings_schema_has_key(schema, "cursor-blink") &&
		   g_settings_schema_has_key(schema, "cursor-shape") &&
		   g_settings_schema_has_key(schema, "follow-system-theme") &&
		   g_settings_schema_has_key(schema, "confirm-close") &&
		   g_settings_schema_has_key(schema, "disable-alt") &&
		   g_settings_schema_has_key(schema, "tab-position") &&
		   g_settings_schema_has_key(schema, "window-width") &&
		   g_settings_schema_has_key(schema, "window-height") &&
		   g_settings_schema_has_key(schema, "window-x") &&
		   g_settings_schema_has_key(schema, "window-y");
}

/**
 * lds_terminal_settings_apply:
 * @terminal: Terminal instance.
 *
 * Apply current settings to a terminal.
 */
void lds_terminal_settings_apply(LdsTerminal *terminal) {
	if (!terminal)
		return;

	lds_terminal_settings_apply_ui_only(terminal);
	lds_terminal_settings_apply_vte_all(terminal);
}

void lds_terminal_settings_apply_ui_only(LdsTerminal *terminal) {
	if (!terminal)
		return;

	lds_terminal_settings_apply_style(lds_terminal_settings_get());

	if (terminal->menu && GTK_IS_WIDGET(terminal->menu)) {
		gtk_widget_set_visible(terminal->menu, !lds_terminal_settings_hide_menu_bar());
	}

	lds_terminal_menu_update_accelerators(terminal);

	gint desired_pos = lds_terminal_settings_tab_position();
	if (terminal->current_tab_position != desired_pos) {
		lds_terminal_tabs_set_position(terminal, desired_pos);
		terminal->current_tab_position = desired_pos;
	}
}

void lds_terminal_settings_apply_vte_all(LdsTerminal *terminal) {
	if (!terminal || !terminal->terms)
		return;

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		if (!term)
			continue;
		lds_terminal_vte_apply_settings(terminal, term);
		lds_terminal_vte_resync_layout(term);
	}
}

void lds_terminal_settings_apply_current(LdsTerminal *terminal) {
	lds_terminal_settings_apply_vte_current(terminal);
}

void lds_terminal_settings_apply_vte_current(LdsTerminal *terminal) {
	if (!terminal)
		return;

	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	lds_terminal_vte_apply_settings(terminal, term);
	lds_terminal_vte_resync_layout(term);
}

/**
 * lds_terminal_settings_apply_to_all:
 * @any_terminal: Any terminal instance.
 *
 * Apply settings to all open windows.
 */
void lds_terminal_settings_apply_to_all(LdsTerminal *any_terminal) {
	if (!any_terminal || !any_terminal->parent)
		return;

	GPtrArray *windows = any_terminal->parent->windows;
	if (!windows || windows->len == 0)
		return;

	for (guint i = 0; i < windows->len; i++) {
		LdsTerminal *t = g_ptr_array_index(windows, i);
		lds_terminal_settings_apply(t);
	}
}

gboolean lds_terminal_settings_confirm_close_enabled(void) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	return !s->disable_confirm;
}

gboolean lds_terminal_settings_confirm_running_process_enabled(void) {
	return lds_terminal_settings_get()->confirm_running_process;
}

gboolean lds_terminal_settings_alt_enabled(void) {
	return !lds_terminal_settings_get()->disable_alt;
}

#define LDS_SETTINGS_ACCEL_GETTER(name, field)                                                     \
	const char *lds_terminal_settings_##name(void) {                                               \
		return lds_terminal_settings_get()->field;                                                 \
	}

#define LDS_SETTINGS_ACCEL_SETTER(name, field)                                                     \
	void lds_terminal_settings_set_##name(const char *accel) {                                     \
		lds_terminal_settings_set_accel_string(&lds_terminal_settings_get()->field, accel);        \
	}

LDS_SETTINGS_ACCEL_GETTER(new_window_accel, new_window_accel)
LDS_SETTINGS_ACCEL_GETTER(new_tab_accel, new_tab_accel)
LDS_SETTINGS_ACCEL_GETTER(close_tab_accel, close_tab_accel)
LDS_SETTINGS_ACCEL_GETTER(rename_tab_accel, rename_tab_accel)
LDS_SETTINGS_ACCEL_GETTER(close_window_accel, close_window_accel)
LDS_SETTINGS_ACCEL_GETTER(preferences_accel, preferences_accel)
LDS_SETTINGS_ACCEL_GETTER(shortcuts_accel, shortcuts_accel)
LDS_SETTINGS_ACCEL_GETTER(copy_accel, copy_accel)
LDS_SETTINGS_ACCEL_GETTER(paste_accel, paste_accel)
LDS_SETTINGS_ACCEL_GETTER(clear_accel, clear_accel)
LDS_SETTINGS_ACCEL_GETTER(reset_accel, reset_accel)
LDS_SETTINGS_ACCEL_GETTER(zoom_in_accel, zoom_in_accel)
LDS_SETTINGS_ACCEL_GETTER(zoom_out_accel, zoom_out_accel)
LDS_SETTINGS_ACCEL_GETTER(zoom_reset_accel, zoom_reset_accel)
LDS_SETTINGS_ACCEL_GETTER(split_vertical_accel, split_vertical_accel)
LDS_SETTINGS_ACCEL_GETTER(close_pane_accel, close_pane_accel)
LDS_SETTINGS_ACCEL_GETTER(focus_next_pane_accel, focus_next_pane_accel)
LDS_SETTINGS_ACCEL_GETTER(about_accel, about_accel)

LDS_SETTINGS_ACCEL_SETTER(new_window_accel, new_window_accel)
LDS_SETTINGS_ACCEL_SETTER(new_tab_accel, new_tab_accel)
LDS_SETTINGS_ACCEL_SETTER(close_tab_accel, close_tab_accel)
LDS_SETTINGS_ACCEL_SETTER(rename_tab_accel, rename_tab_accel)
LDS_SETTINGS_ACCEL_SETTER(close_window_accel, close_window_accel)
LDS_SETTINGS_ACCEL_SETTER(preferences_accel, preferences_accel)
LDS_SETTINGS_ACCEL_SETTER(shortcuts_accel, shortcuts_accel)
LDS_SETTINGS_ACCEL_SETTER(copy_accel, copy_accel)
LDS_SETTINGS_ACCEL_SETTER(paste_accel, paste_accel)
LDS_SETTINGS_ACCEL_SETTER(clear_accel, clear_accel)
LDS_SETTINGS_ACCEL_SETTER(reset_accel, reset_accel)
LDS_SETTINGS_ACCEL_SETTER(zoom_in_accel, zoom_in_accel)
LDS_SETTINGS_ACCEL_SETTER(zoom_out_accel, zoom_out_accel)
LDS_SETTINGS_ACCEL_SETTER(zoom_reset_accel, zoom_reset_accel)
LDS_SETTINGS_ACCEL_SETTER(split_vertical_accel, split_vertical_accel)
LDS_SETTINGS_ACCEL_SETTER(close_pane_accel, close_pane_accel)
LDS_SETTINGS_ACCEL_SETTER(focus_next_pane_accel, focus_next_pane_accel)
LDS_SETTINGS_ACCEL_SETTER(about_accel, about_accel)

#undef LDS_SETTINGS_ACCEL_GETTER
#undef LDS_SETTINGS_ACCEL_SETTER

/* VTE queries */
const char *lds_terminal_settings_font_name(void) {
	return lds_terminal_settings_get()->font_name;
}

guint lds_terminal_settings_scrollback(void) {
	return lds_terminal_settings_get()->scrollback;
}

void lds_terminal_settings_set_scrollback(guint lines) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	if (lines < 1000)
		lines = 1000;
	if (lines > 50000)
		lines = 50000;

	s->scrollback = lines;
	if (settings)
		g_settings_set_uint(settings, "scrollback", s->scrollback);
}

gboolean lds_terminal_settings_cursor_blink(void) {
	return lds_terminal_settings_get()->cursor_blink;
}

gint lds_terminal_settings_cursor_shape(void) {
	return lds_terminal_settings_get()->cursor_shape;
}

void lds_terminal_settings_set_cursor_shape(gint shape) {
	lds_terminal_settings_get()->cursor_shape = shape;
	if (settings)
		g_settings_set_int(settings, "cursor-shape", shape);
}

gboolean lds_terminal_settings_follow_system_theme(void) {
	return lds_terminal_settings_get()->follow_system_theme;
}

gint lds_terminal_settings_theme_mode(void) {
	return lds_terminal_settings_get()->theme_mode;
}

void lds_terminal_settings_set_theme_mode(gint mode) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	switch (mode) {
	case LDS_TERMINAL_THEME_DARK:
		s->follow_system_theme = FALSE;
		s->theme_mode = LDS_TERMINAL_THEME_DARK;
		break;
	case LDS_TERMINAL_THEME_LIGHT:
		s->follow_system_theme = FALSE;
		s->theme_mode = LDS_TERMINAL_THEME_LIGHT;
		break;
	case LDS_TERMINAL_THEME_SYSTEM:
	default:
		s->follow_system_theme = TRUE;
		s->theme_mode = LDS_TERMINAL_THEME_SYSTEM;
		break;
	}

	if (settings) {
		g_settings_set_boolean(settings, "follow-system-theme", s->follow_system_theme);
		if (settings_has_theme_mode)
			g_settings_set_int(settings, "theme-mode", s->theme_mode);
	}

	lds_terminal_settings_apply_style(s);
}

void lds_terminal_settings_set_follow_system_theme(gboolean follow) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->follow_system_theme = follow;
	if (follow)
		s->theme_mode = LDS_TERMINAL_THEME_SYSTEM;
	else if (s->theme_mode == LDS_TERMINAL_THEME_SYSTEM)
		s->theme_mode = LDS_TERMINAL_THEME_DARK;

	if (settings) {
		g_settings_set_boolean(settings, "follow-system-theme", follow);
		if (settings_has_theme_mode)
			g_settings_set_int(settings, "theme-mode", s->theme_mode);
	}

	lds_terminal_settings_apply_style(s);
}

gboolean lds_terminal_settings_sync_prompt_colors(void) {
	return lds_terminal_settings_get()->sync_prompt_colors;
}

void lds_terminal_settings_set_sync_prompt_colors(gboolean enabled) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->sync_prompt_colors = enabled;
	if (settings && settings_has_sync_prompt_colors)
		g_settings_set_boolean(settings, "sync-prompt-colors", enabled);
}

GSettings *lds_terminal_settings_backend(void) {
	lds_terminal_settings_load_from_gsettings();
	return settings;
}

void lds_terminal_settings_set_hide_menu_bar(gboolean hide) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->hide_menu_bar = hide;
	if (settings && settings_has_hide_menu_bar)
		g_settings_set_boolean(settings, "hide-menu-bar", hide);
}

void lds_terminal_settings_set_strict_determinism(gboolean enabled) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->strict_determinism = enabled;
	if (settings && settings_has_strict_determinism)
		g_settings_set_boolean(settings, "strict-determinism", enabled);
	lds_terminal_settings_apply_determinism(s);
}

void lds_terminal_settings_set_confirm_close(gboolean confirm) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->disable_confirm = !confirm;
	if (settings)
		g_settings_set_boolean(settings, "confirm-close", confirm);
}

void lds_terminal_settings_set_confirm_running_process(gboolean confirm) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->confirm_running_process = confirm;
	if (settings && settings_has_confirm_running_process)
		g_settings_set_boolean(settings, "confirm-running-process", confirm);
}

void lds_terminal_settings_set_disable_alt(gboolean disable) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->disable_alt = disable;
	if (settings)
		g_settings_set_boolean(settings, "disable-alt", disable);
}

void lds_terminal_settings_set_tab_position(gint position) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->tab_position = position;
	if (settings)
		g_settings_set_int(settings, "tab-position", position);
}

void lds_terminal_settings_set_cursor_blink(gboolean blink) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	s->cursor_blink = blink;
	if (settings)
		g_settings_set_boolean(settings, "cursor-blink", blink);
}

gboolean lds_terminal_settings_hide_menu_bar(void) {
	return lds_terminal_settings_get()->hide_menu_bar;
}

gboolean lds_terminal_settings_strict_determinism(void) {
	return lds_terminal_settings_get()->strict_determinism;
}

gint lds_terminal_settings_tab_position(void) {
	return lds_terminal_settings_get()->tab_position;
}

gint lds_terminal_settings_window_width(void) {
	return lds_terminal_settings_get()->window_width;
}

gint lds_terminal_settings_window_height(void) {
	return lds_terminal_settings_get()->window_height;
}

gint lds_terminal_settings_window_x(void) {
	return lds_terminal_settings_get()->window_x;
}

gint lds_terminal_settings_window_y(void) {
	return lds_terminal_settings_get()->window_y;
}

void lds_terminal_settings_set_window_geometry(gint width, gint height, gint x, gint y) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	if (width > 0) {
		if (width < lds_terminal_settings_min_window_size)
			width = lds_terminal_settings_min_window_size;
		s->window_width = width;
		if (settings)
			g_settings_set_int(settings, "window-width", width);
	}

	if (height > 0) {
		if (height < lds_terminal_settings_min_window_size)
			height = lds_terminal_settings_min_window_size;
		s->window_height = height;
		if (settings)
			g_settings_set_int(settings, "window-height", height);
	}

	s->window_x = x;
	s->window_y = y;
	if (settings && x >= 0 && y >= 0) {
		g_settings_set_int(settings, "window-x", x);
		g_settings_set_int(settings, "window-y", y);
	}
}

gboolean lds_terminal_settings_background_transparent(void) {
	return lds_terminal_settings_get()->background_color.alpha < 1.0;
}

static struct _LdsTerminalSettings global_setting = {0};

static void lds_terminal_settings_clear_owned_strings(void) {
	g_clear_pointer(&global_setting.new_window_accel, g_free);
	g_clear_pointer(&global_setting.new_tab_accel, g_free);
	g_clear_pointer(&global_setting.close_tab_accel, g_free);
	g_clear_pointer(&global_setting.rename_tab_accel, g_free);
	g_clear_pointer(&global_setting.close_window_accel, g_free);
	g_clear_pointer(&global_setting.preferences_accel, g_free);
	g_clear_pointer(&global_setting.shortcuts_accel, g_free);
	g_clear_pointer(&global_setting.copy_accel, g_free);
	g_clear_pointer(&global_setting.paste_accel, g_free);
	g_clear_pointer(&global_setting.clear_accel, g_free);
	g_clear_pointer(&global_setting.reset_accel, g_free);
	g_clear_pointer(&global_setting.zoom_in_accel, g_free);
	g_clear_pointer(&global_setting.zoom_out_accel, g_free);
	g_clear_pointer(&global_setting.zoom_reset_accel, g_free);
	g_clear_pointer(&global_setting.split_vertical_accel, g_free);
	g_clear_pointer(&global_setting.close_pane_accel, g_free);
	g_clear_pointer(&global_setting.focus_next_pane_accel, g_free);
	g_clear_pointer(&global_setting.about_accel, g_free);
	g_clear_pointer(&global_setting.font_name, g_free);
}

static void lds_terminal_settings_apply_style(const LdsTerminalSettings *s) {
	if (!s || !gtk_is_initialized())
		return;

	lds_terminal_settings_apply_determinism(s);

	AdwStyleManager *style = adw_style_manager_get_default();
	if (s->theme_mode == LDS_TERMINAL_THEME_SYSTEM) {
		adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_DEFAULT);
	} else if (s->theme_mode == LDS_TERMINAL_THEME_DARK) {
		adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_PREFER_DARK);
	} else {
		adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_PREFER_LIGHT);
	}
}

static void lds_terminal_settings_apply_determinism(const LdsTerminalSettings *s) {
	if (!s || !gtk_is_initialized())
		return;

	GtkSettings *gtk_settings = gtk_settings_get_default();
	if (!gtk_settings)
		return;

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(gtk_settings), "gtk-enable-animations")) {
		g_object_set(gtk_settings, "gtk-enable-animations", !s->strict_determinism, NULL);
	}
	if (g_object_class_find_property(G_OBJECT_GET_CLASS(gtk_settings), "gtk-overlay-scrolling")) {
		g_object_set(gtk_settings, "gtk-overlay-scrolling", !s->strict_determinism, NULL);
	}
	if (g_object_class_find_property(G_OBJECT_GET_CLASS(gtk_settings),
									 "gtk-primary-button-warps-slider")) {
		g_object_set(gtk_settings, "gtk-primary-button-warps-slider", !s->strict_determinism, NULL);
	}
}

static void lds_terminal_settings_apply_builtin_defaults(void) {
	lds_terminal_settings_clear_owned_strings();
	global_setting.disable_confirm = FALSE;
	global_setting.confirm_running_process = TRUE;
	global_setting.disable_alt = FALSE;
	global_setting.new_window_accel = g_strdup("<Ctrl><Shift>N");
	global_setting.new_tab_accel = g_strdup("<Ctrl><Shift>T");
	global_setting.close_tab_accel = g_strdup("<Ctrl><Shift>W");
	global_setting.rename_tab_accel = g_strdup("<Ctrl><Shift>R");
	global_setting.close_window_accel = g_strdup("<Ctrl><Shift>Q");
	global_setting.preferences_accel = g_strdup("<Ctrl>comma");
	global_setting.shortcuts_accel = g_strdup("<Ctrl><Shift>slash");
	global_setting.copy_accel = g_strdup("<Ctrl><Shift>C");
	global_setting.paste_accel = g_strdup("<Ctrl><Shift>V");
	global_setting.clear_accel = g_strdup("<Ctrl><Shift>K");
	global_setting.reset_accel = g_strdup("<Ctrl><Shift>L");
	global_setting.zoom_in_accel = g_strdup("<Ctrl>plus");
	global_setting.zoom_out_accel = g_strdup("<Ctrl>minus");
	global_setting.zoom_reset_accel = g_strdup("<Ctrl>0");
	global_setting.split_vertical_accel = g_strdup("<Ctrl><Shift>J");
	global_setting.close_pane_accel = g_strdup("<Ctrl><Shift>D");
	global_setting.focus_next_pane_accel = g_strdup("<Ctrl><Shift>Tab");
	global_setting.about_accel = g_strdup("<Ctrl><Shift>A");
	global_setting.font_name = g_strdup("Monospace 10");
	global_setting.scrollback = 10000;
	global_setting.cursor_blink = TRUE;
	global_setting.cursor_shape = 1; /* VTE_CURSOR_SHAPE_IBEAM */
	global_setting.font_color = (GdkRGBA){.red = 0.90, .green = 0.90, .blue = 0.90, .alpha = 1.0};
	global_setting.hide_menu_bar = FALSE;
	global_setting.strict_determinism = TRUE;
	global_setting.tab_position = GTK_POS_TOP;
	global_setting.follow_system_theme = FALSE;
	global_setting.theme_mode = LDS_TERMINAL_THEME_DARK;
	global_setting.sync_prompt_colors = TRUE;
	global_setting.background_color =
		(GdkRGBA){.red = 0.12941176, .green = 0.14509804, .blue = 0.16078431, .alpha = 1.0};
	global_setting.window_width = 800;
	global_setting.window_height = 500;
	global_setting.window_x = -1;
	global_setting.window_y = -1;
}

static void lds_terminal_settings_load_from_gsettings(void) {
	g_mutex_lock(&settings_lock);
	if (settings_loaded)
		goto out;

	lds_terminal_settings_apply_builtin_defaults();

	if (!settings) {
		const char *schema_dir_env = g_getenv("GSETTINGS_SCHEMA_DIR");
		gboolean allow_dev_schema = g_strcmp0(g_getenv("LDS_TERMINAL_DEV_SCHEMA"), "1") == 0;
		GSettingsSchema *schema = NULL;

		if (schema_dir_env && *schema_dir_env) {
			schema = lds_terminal_settings_try_schema_dir(schema_dir_env, NULL);
			if (!schema) {
				g_message("GSettings schema bar.astware.lds-terminal not found in %s.",
						  schema_dir_env);
			}
		} else {
			GSettingsSchemaSource *source = g_settings_schema_source_get_default();
			if (source)
				schema = g_settings_schema_source_lookup(source, "bar.astware.lds-terminal", TRUE);
			if (schema && !lds_terminal_settings_schema_is_compatible(schema)) {
				g_settings_schema_unref(schema);
				schema = NULL;
			}

			if (!schema && allow_dev_schema) {
				g_autofree gchar *exe_dir = lds_terminal_settings_exe_dir();
				if (exe_dir) {
					/* Meson layout: build/src/lds-terminal -> build/data/gschemas.compiled. */
					g_autofree gchar *build_dir = g_build_filename(exe_dir, "..", "data", NULL);
					schema = lds_terminal_settings_try_schema_dir(build_dir, NULL);
					if (schema && !lds_terminal_settings_schema_is_compatible(schema)) {
						g_settings_schema_unref(schema);
						schema = NULL;
					}
				}
				if (!schema) {
					g_autofree gchar *cwd = g_get_current_dir();
					g_autofree gchar *cwd_build_dir = g_build_filename(cwd, "build", "data", NULL);
					schema = lds_terminal_settings_try_schema_dir(cwd_build_dir, NULL);
					if (schema && !lds_terminal_settings_schema_is_compatible(schema)) {
						g_settings_schema_unref(schema);
						schema = NULL;
					}
				}
				if (!schema) {
					g_message("LDS_TERMINAL_DEV_SCHEMA=1 but schema bar.astware.lds-terminal was not "
							  "found in local development lookup paths.");
				}
			} else if (!schema) {
				g_message("GSettings schema bar.astware.lds-terminal not found in default locations; "
						  "set LDS_TERMINAL_DEV_SCHEMA=1 to enable local schema lookup.");
			}
		}

		if (schema) {
			settings_has_sync_prompt_colors =
				g_settings_schema_has_key(schema, "sync-prompt-colors");
			settings_has_font_color = g_settings_schema_has_key(schema, "font-color");
			settings_has_background_color = g_settings_schema_has_key(schema, "background-color");
			settings_has_theme_mode = g_settings_schema_has_key(schema, "theme-mode");
			settings_has_hide_menu_bar = g_settings_schema_has_key(schema, "hide-menu-bar");
			settings_has_strict_determinism =
				g_settings_schema_has_key(schema, "strict-determinism");
			settings_has_confirm_running_process =
				g_settings_schema_has_key(schema, "confirm-running-process");
			settings = g_settings_new_full(schema, NULL, NULL);
			g_settings_schema_unref(schema);
		}
	}

	if (settings) {
		g_autofree gchar *font = g_settings_get_string(settings, "font-name");
		g_free(global_setting.font_name);
		global_setting.font_name = g_strdup(font && *font ? font : "Monospace 10");

		global_setting.scrollback = g_settings_get_uint(settings, "scrollback");
		global_setting.scrollback = CLAMP(global_setting.scrollback, 1000, 50000);
		global_setting.cursor_blink = g_settings_get_boolean(settings, "cursor-blink");
		global_setting.cursor_shape = g_settings_get_int(settings, "cursor-shape");
		if (global_setting.cursor_shape < 0 || global_setting.cursor_shape > 2)
			global_setting.cursor_shape = 1; /* VTE_CURSOR_SHAPE_IBEAM */
		global_setting.follow_system_theme =
			g_settings_get_boolean(settings, "follow-system-theme");
		if (settings_has_theme_mode) {
			gint mode = g_settings_get_int(settings, "theme-mode");
			if (mode < LDS_TERMINAL_THEME_SYSTEM || mode > LDS_TERMINAL_THEME_DARK)
				mode = LDS_TERMINAL_THEME_DARK;
			global_setting.theme_mode = mode;
			global_setting.follow_system_theme = (mode == LDS_TERMINAL_THEME_SYSTEM);
		} else {
			global_setting.theme_mode = global_setting.follow_system_theme
											? LDS_TERMINAL_THEME_SYSTEM
											: LDS_TERMINAL_THEME_DARK;
		}
		if (settings_has_hide_menu_bar)
			global_setting.hide_menu_bar = g_settings_get_boolean(settings, "hide-menu-bar");
		else
			global_setting.hide_menu_bar = FALSE;
		if (settings_has_strict_determinism)
			global_setting.strict_determinism =
				g_settings_get_boolean(settings, "strict-determinism");
		else
			global_setting.strict_determinism = TRUE;
		global_setting.disable_confirm = !g_settings_get_boolean(settings, "confirm-close");
		if (settings_has_confirm_running_process) {
			global_setting.confirm_running_process =
				g_settings_get_boolean(settings, "confirm-running-process");
		} else {
			global_setting.confirm_running_process = TRUE;
		}
		global_setting.disable_alt = g_settings_get_boolean(settings, "disable-alt");
		global_setting.tab_position = g_settings_get_int(settings, "tab-position");
		global_setting.window_width = g_settings_get_int(settings, "window-width");
		global_setting.window_height = g_settings_get_int(settings, "window-height");
		if (global_setting.window_width < lds_terminal_settings_min_window_size)
			global_setting.window_width = lds_terminal_settings_min_window_size;
		if (global_setting.window_height < lds_terminal_settings_min_window_size)
			global_setting.window_height = lds_terminal_settings_min_window_size;
		global_setting.window_x = g_settings_get_int(settings, "window-x");
		global_setting.window_y = g_settings_get_int(settings, "window-y");
		if (settings_has_sync_prompt_colors)
			global_setting.sync_prompt_colors =
				g_settings_get_boolean(settings, "sync-prompt-colors");
		else
			global_setting.sync_prompt_colors = TRUE;
		if (settings_has_font_color) {
			g_autofree gchar *font_color_str = g_settings_get_string(settings, "font-color");
			GdkRGBA parsed = {0};
			if (lds_terminal_settings_rgba_from_string(font_color_str, &parsed))
				global_setting.font_color = parsed;
		}
		if (settings_has_background_color) {
			g_autofree gchar *bg_color_str = g_settings_get_string(settings, "background-color");
			GdkRGBA parsed = {0};
			if (lds_terminal_settings_rgba_from_string(bg_color_str, &parsed))
				global_setting.background_color = parsed;
		}
	}

	/* Intentional: avoid repeated schema probing when no backend schema exists. */
	settings_loaded = TRUE;
out:
	g_mutex_unlock(&settings_lock);
}

static GSettingsSchema *lds_terminal_settings_try_schema_dir(const gchar *dir,
															 GSettingsSchemaSource *parent) {
	if (!dir || !g_file_test(dir, G_FILE_TEST_IS_DIR))
		return NULL;

	g_autoptr(GError) error = NULL;
	GSettingsSchemaSource *local =
		g_settings_schema_source_new_from_directory(dir, parent, FALSE, &error);
	if (!local)
		return NULL;

	GSettingsSchema *schema = g_settings_schema_source_lookup(local, "bar.astware.lds-terminal", TRUE);
	g_settings_schema_source_unref(local);
	return schema;
}

static gchar *lds_terminal_settings_exe_dir(void) {
	g_autoptr(GError) error = NULL;
	g_autofree gchar *exe_path = NULL;
#ifdef __linux__
	exe_path = g_file_read_link("/proc/self/exe", &error);
#elif defined(__FreeBSD__)
	/* FreeBSD may not have procfs mounted; caller handles NULL path. */
	exe_path = g_file_read_link("/proc/curproc/file", &error);
#endif
	if (!exe_path || !*exe_path)
		return NULL;

	return g_path_get_dirname(exe_path);
}

/**
 * lds_terminal_settings_get:
 *
 * Return the global settings instance.
 *
 * Main-thread only.
 */
LdsTerminalSettings *lds_terminal_settings_get(void) {
	lds_terminal_settings_load_from_gsettings();
	return (LdsTerminalSettings *)&global_setting;
}

void lds_terminal_settings_set_font_name(const char *font_name) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s)
		return;

	g_free(s->font_name);
	s->font_name = g_strdup(font_name && *font_name ? font_name : "Monospace 10");
	if (settings)
		g_settings_set_string(settings, "font-name", s->font_name);
}

const GdkRGBA *lds_terminal_settings_font_color(void) {
	return &lds_terminal_settings_get()->font_color;
}

const GdkRGBA *lds_terminal_settings_background_color(void) {
	return &lds_terminal_settings_get()->background_color;
}

void lds_terminal_settings_set_font_color(const GdkRGBA *color) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s || !color)
		return;

	s->font_color = *color;
	if (settings && settings_has_font_color) {
		g_autofree gchar *str = lds_terminal_settings_rgba_to_string(color);
		g_settings_set_string(settings, "font-color", str);
	}
}

void lds_terminal_settings_set_background_color(const GdkRGBA *color) {
	LdsTerminalSettings *s = lds_terminal_settings_get();
	if (!s || !color)
		return;

	s->background_color = *color;
	if (settings && settings_has_background_color) {
		g_autofree gchar *str = lds_terminal_settings_rgba_to_string(color);
		g_settings_set_string(settings, "background-color", str);
	}
}

void lds_terminal_settings_reset_defaults(void) {
	lds_terminal_settings_load_from_gsettings();

	if (!settings) {
		g_mutex_lock(&settings_lock);
		lds_terminal_settings_apply_builtin_defaults();
		settings_loaded = FALSE;
		g_mutex_unlock(&settings_lock);
		return;
	}

	g_settings_reset(settings, "font-name");
	if (settings_has_font_color)
		g_settings_reset(settings, "font-color");
	if (settings_has_background_color)
		g_settings_reset(settings, "background-color");
	g_settings_reset(settings, "scrollback");
	g_settings_reset(settings, "cursor-blink");
	g_settings_reset(settings, "cursor-shape");
	g_settings_reset(settings, "follow-system-theme");
	if (settings_has_theme_mode)
		g_settings_reset(settings, "theme-mode");
	if (settings_has_hide_menu_bar)
		g_settings_reset(settings, "hide-menu-bar");
	if (settings_has_strict_determinism)
		g_settings_reset(settings, "strict-determinism");
	g_settings_reset(settings, "confirm-close");
	if (settings_has_confirm_running_process)
		g_settings_reset(settings, "confirm-running-process");
	g_settings_reset(settings, "disable-alt");
	g_settings_reset(settings, "tab-position");
	g_settings_reset(settings, "window-width");
	g_settings_reset(settings, "window-height");
	g_settings_reset(settings, "window-x");
	g_settings_reset(settings, "window-y");
	if (settings_has_sync_prompt_colors)
		g_settings_reset(settings, "sync-prompt-colors");

	g_mutex_lock(&settings_lock);
	settings_loaded = FALSE;
	g_mutex_unlock(&settings_lock);
	lds_terminal_settings_load_from_gsettings();
}

static gchar *lds_terminal_settings_rgba_to_string(const GdkRGBA *color) {
	if (!color)
		return g_strdup("rgba(0,0,0,1)");

	guint red = (guint)(CLAMP(color->red, 0.0, 1.0) * 255.0 + 0.5);
	guint green = (guint)(CLAMP(color->green, 0.0, 1.0) * 255.0 + 0.5);
	guint blue = (guint)(CLAMP(color->blue, 0.0, 1.0) * 255.0 + 0.5);
	gdouble alpha = CLAMP(color->alpha, 0.0, 1.0);

	return g_strdup_printf("rgba(%u,%u,%u,%.3f)", red, green, blue, alpha);
}

static gboolean lds_terminal_settings_rgba_from_string(const char *value, GdkRGBA *out) {
	if (!value || !*value || !out)
		return FALSE;

	return gdk_rgba_parse(out, value);
}

static void lds_terminal_settings_set_accel_string(gchar **slot, const char *accel) {
	if (!slot)
		return;

	g_autofree gchar *normalized = lds_terminal_shortcuts_normalize_accel(accel);
	g_free(*slot);
	*slot = g_strdup(normalized ? normalized : "");
}
