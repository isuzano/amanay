/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Internal settings state and helpers.
 */

#ifndef LDS_TERMINAL_SETTINGS_INTERNAL_H
#define LDS_TERMINAL_SETTINGS_INTERNAL_H

#include <glib.h>
#include <gdk/gdkrgba.h>

/* Settings container used by settings.c */
struct _LdsTerminalSettings {
	/* Behavior */
	gboolean disable_confirm;
	gboolean confirm_running_process;
	gboolean disable_alt;

	/* Shortcuts */
	gchar *new_window_accel;
	gchar *new_tab_accel;
	gchar *close_tab_accel;
	gchar *rename_tab_accel;
	gchar *close_window_accel;
	gchar *preferences_accel;
	gchar *shortcuts_accel;
	gchar *copy_accel;
	gchar *paste_accel;
	gchar *clear_accel;
	gchar *reset_accel;
	gchar *zoom_in_accel;
	gchar *zoom_out_accel;
	gchar *zoom_reset_accel;
	gchar *split_vertical_accel;
	gchar *close_pane_accel;
	gchar *focus_next_pane_accel;
	gchar *about_accel;

	/* VTE */
	gchar *font_name;
	guint scrollback;
	gboolean cursor_blink;
	gint cursor_shape;
	GdkRGBA font_color;

	/* UI */
	gboolean hide_menu_bar;
	gboolean strict_determinism;
	gint tab_position;
	gboolean follow_system_theme;
	gint theme_mode;
	gboolean sync_prompt_colors;

	/* Window */
	GdkRGBA background_color;
	gint window_width;
	gint window_height;
	gint window_x;
	gint window_y;

	/* State */
};

#endif /* LDS_TERMINAL_SETTINGS_INTERNAL_H */
