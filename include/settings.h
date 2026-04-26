/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Runtime settings API.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <glib.h>
#include <gio/gio.h>
#include <gdk/gdk.h>

/**
 * LdsTerminalSettings:
 *
 * Opaque settings container for the terminal.
 */
typedef struct _LdsTerminalSettings LdsTerminalSettings;
typedef struct _LdsTerminal LdsTerminal;

typedef enum {
	LDS_TERMINAL_THEME_SYSTEM = 0,
	LDS_TERMINAL_THEME_LIGHT = 1,
	LDS_TERMINAL_THEME_DARK = 2
} LdsTerminalThemeMode;

/**
 * lds_terminal_settings_get:
 *
 * Return the global settings instance.
 * Must be called from the GTK main thread.
 *
 * Returns: (transfer none): The #LdsTerminalSettings singleton.
 */
LdsTerminalSettings *lds_terminal_settings_get(void);

/**
 * lds_terminal_settings_confirm_close_enabled:
 *
 * Return whether close confirmation is enabled.
 *
 * Returns: %TRUE when enabled.
 */
gboolean lds_terminal_settings_confirm_close_enabled(void);

/**
 * lds_terminal_settings_confirm_running_process_enabled:
 *
 * Return whether confirmation is required before terminating running jobs.
 *
 * Returns: %TRUE when enabled.
 */
gboolean lds_terminal_settings_confirm_running_process_enabled(void);
/**
 * lds_terminal_settings_alt_enabled:
 *
 * Return whether Alt mnemonics are enabled.
 *
 * Returns: %TRUE when enabled.
 */
gboolean lds_terminal_settings_alt_enabled(void);

/**
 * lds_terminal_settings_new_window_accel:
 *
 * Return accelerator for new window.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_new_window_accel(void);
/**
 * lds_terminal_settings_new_tab_accel:
 *
 * Return accelerator for new tab.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_new_tab_accel(void);
/**
 * lds_terminal_settings_close_tab_accel:
 *
 * Return accelerator for close tab.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_close_tab_accel(void);

/**
 * lds_terminal_settings_rename_tab_accel:
 *
 * Return accelerator for rename tab.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_rename_tab_accel(void);
/**
 * lds_terminal_settings_close_window_accel:
 *
 * Return accelerator for close window.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_close_window_accel(void);

/**
 * lds_terminal_settings_preferences_accel:
 *
 * Return accelerator for preferences.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_preferences_accel(void);

/**
 * lds_terminal_settings_shortcuts_accel:
 *
 * Return accelerator for shortcuts dialog.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_shortcuts_accel(void);
/**
 * lds_terminal_settings_copy_accel:
 *
 * Return accelerator for copy.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_copy_accel(void);
/**
 * lds_terminal_settings_paste_accel:
 *
 * Return accelerator for paste.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_paste_accel(void);

/**
 * lds_terminal_settings_clear_accel:
 *
 * Return accelerator for clear action.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_clear_accel(void);

/**
 * lds_terminal_settings_reset_accel:
 *
 * Return accelerator for reset action.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_reset_accel(void);
/**
 * lds_terminal_settings_zoom_in_accel:
 *
 * Return accelerator for zoom in.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_zoom_in_accel(void);
/**
 * lds_terminal_settings_zoom_out_accel:
 *
 * Return accelerator for zoom out.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_zoom_out_accel(void);
/**
 * lds_terminal_settings_zoom_reset_accel:
 *
 * Return accelerator for zoom reset.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_zoom_reset_accel(void);

/**
 * lds_terminal_settings_split_vertical_accel:
 *
 * Return accelerator for split pane.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_split_vertical_accel(void);

/**
 * lds_terminal_settings_close_pane_accel:
 *
 * Return accelerator for close pane.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_close_pane_accel(void);

/**
 * lds_terminal_settings_focus_next_pane_accel:
 *
 * Return accelerator for focusing next pane.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_focus_next_pane_accel(void);

/**
 * lds_terminal_settings_about_accel:
 *
 * Return accelerator for about dialog.
 *
 * Returns: (nullable) (transfer none): Accelerator string.
 */
const char *lds_terminal_settings_about_accel(void);

/**
 * Shortcut setter APIs:
 *
 * Update accelerator bindings used by runtime action registration.
 * Passing NULL or empty string clears the accelerator.
 */
/**
 * lds_terminal_settings_set_new_window_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for new window action.
 */
void lds_terminal_settings_set_new_window_accel(const char *accel);
/**
 * lds_terminal_settings_set_new_tab_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for new tab action.
 */
void lds_terminal_settings_set_new_tab_accel(const char *accel);
/**
 * lds_terminal_settings_set_close_tab_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for close tab action.
 */
void lds_terminal_settings_set_close_tab_accel(const char *accel);
/**
 * lds_terminal_settings_set_rename_tab_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for rename tab action.
 */
void lds_terminal_settings_set_rename_tab_accel(const char *accel);
/**
 * lds_terminal_settings_set_close_window_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for close window action.
 */
void lds_terminal_settings_set_close_window_accel(const char *accel);
/**
 * lds_terminal_settings_set_preferences_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for preferences action.
 */
void lds_terminal_settings_set_preferences_accel(const char *accel);
/**
 * lds_terminal_settings_set_shortcuts_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for shortcuts action.
 */
void lds_terminal_settings_set_shortcuts_accel(const char *accel);
/**
 * lds_terminal_settings_set_copy_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for copy action.
 */
void lds_terminal_settings_set_copy_accel(const char *accel);
/**
 * lds_terminal_settings_set_paste_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for paste action.
 */
void lds_terminal_settings_set_paste_accel(const char *accel);
/**
 * lds_terminal_settings_set_clear_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for clear action.
 */
void lds_terminal_settings_set_clear_accel(const char *accel);
/**
 * lds_terminal_settings_set_reset_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for reset action.
 */
void lds_terminal_settings_set_reset_accel(const char *accel);
/**
 * lds_terminal_settings_set_zoom_in_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for zoom in action.
 */
void lds_terminal_settings_set_zoom_in_accel(const char *accel);
/**
 * lds_terminal_settings_set_zoom_out_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for zoom out action.
 */
void lds_terminal_settings_set_zoom_out_accel(const char *accel);
/**
 * lds_terminal_settings_set_zoom_reset_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for zoom reset action.
 */
void lds_terminal_settings_set_zoom_reset_accel(const char *accel);
/**
 * lds_terminal_settings_set_split_vertical_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for split vertical action.
 */
void lds_terminal_settings_set_split_vertical_accel(const char *accel);
/**
 * lds_terminal_settings_set_close_pane_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for close pane action.
 */
void lds_terminal_settings_set_close_pane_accel(const char *accel);
/**
 * lds_terminal_settings_set_focus_next_pane_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for focus-next-pane action.
 */
void lds_terminal_settings_set_focus_next_pane_accel(const char *accel);
/**
 * lds_terminal_settings_set_about_accel:
 * @accel: (nullable): Accelerator string.
 *
 * Set accelerator for about action.
 */
void lds_terminal_settings_set_about_accel(const char *accel);

/**
 * lds_terminal_settings_font_name:
 *
 * Return font name.
 *
 * Returns: (nullable) (transfer none): Font name.
 */
const char *lds_terminal_settings_font_name(void);

/**
 * lds_terminal_settings_set_font_name:
 * @font_name: (nullable): Font description.
 *
 * Set terminal font description.
 */
void lds_terminal_settings_set_font_name(const char *font_name);

/**
 * lds_terminal_settings_font_color:
 *
 * Return configured foreground color.
 *
 * Returns: (transfer none): Pointer to internal #GdkRGBA.
 */
const GdkRGBA *lds_terminal_settings_font_color(void);

/**
 * lds_terminal_settings_background_color:
 *
 * Return configured background color.
 *
 * Returns: (transfer none): Pointer to internal #GdkRGBA.
 */
const GdkRGBA *lds_terminal_settings_background_color(void);

/**
 * lds_terminal_settings_set_font_color:
 * @color: (not nullable): Foreground color.
 *
 * Set terminal foreground color.
 */
void lds_terminal_settings_set_font_color(const GdkRGBA *color);

/**
 * lds_terminal_settings_set_background_color:
 * @color: (not nullable): Background color.
 *
 * Set terminal background color.
 */
void lds_terminal_settings_set_background_color(const GdkRGBA *color);
/**
 * lds_terminal_settings_scrollback:
 *
 * Return scrollback lines.
 *
 * Returns: Scrollback line count.
 */
guint lds_terminal_settings_scrollback(void);
/**
 * lds_terminal_settings_set_scrollback:
 * @lines: Scrollback line count.
 *
 * Set scrollback line limit.
 */
void lds_terminal_settings_set_scrollback(guint lines);
/**
 * lds_terminal_settings_cursor_blink:
 *
 * Return cursor blink state.
 *
 * Returns: %TRUE when enabled.
 */
gboolean lds_terminal_settings_cursor_blink(void);
/**
 * lds_terminal_settings_set_cursor_blink:
 * @blink: %TRUE enables blinking cursor.
 *
 * Set cursor blink behavior.
 */
void lds_terminal_settings_set_cursor_blink(gboolean blink);

/**
 * lds_terminal_settings_cursor_shape:
 *
 * Return configured cursor shape enum value.
 *
 * Returns: Cursor shape value.
 */
gint lds_terminal_settings_cursor_shape(void);

/**
 * lds_terminal_settings_set_cursor_shape:
 * @shape: Cursor shape enum value.
 *
 * Set cursor shape.
 */
void lds_terminal_settings_set_cursor_shape(gint shape);

/**
 * lds_terminal_settings_follow_system_theme:
 *
 * Return whether style follows system theme.
 *
 * Returns: %TRUE when following system.
 */
gboolean lds_terminal_settings_follow_system_theme(void);
/**
 * lds_terminal_settings_set_follow_system_theme:
 * @follow: %TRUE to follow system theme.
 *
 * Set whether style follows system theme.
 */
void lds_terminal_settings_set_follow_system_theme(gboolean follow);
/**
 * lds_terminal_settings_theme_mode:
 *
 * Return explicit theme mode.
 *
 * Returns: #LdsTerminalThemeMode value.
 */
gint lds_terminal_settings_theme_mode(void);
/**
 * lds_terminal_settings_set_theme_mode:
 * @mode: #LdsTerminalThemeMode value.
 *
 * Set explicit theme mode.
 */
void lds_terminal_settings_set_theme_mode(gint mode);
/**
 * lds_terminal_settings_sync_prompt_colors:
 *
 * Return whether prompt colors are synchronized.
 *
 * Returns: %TRUE when prompt color sync is enabled.
 */
gboolean lds_terminal_settings_sync_prompt_colors(void);
/**
 * lds_terminal_settings_set_sync_prompt_colors:
 * @enabled: %TRUE to enable prompt color synchronization.
 *
 * Set prompt color synchronization behavior.
 */
void lds_terminal_settings_set_sync_prompt_colors(gboolean enabled);

/**
 * lds_terminal_settings_backend:
 *
 * Return backend #GSettings instance when available.
 *
 * Returns: (nullable) (transfer none): Settings backend.
 */
GSettings *lds_terminal_settings_backend(void);
/**
 * lds_terminal_settings_reset_defaults:
 *
 * Restore default values for all persisted settings.
 */
void lds_terminal_settings_reset_defaults(void);
/**
 * lds_terminal_settings_set_confirm_close:
 * @confirm: %TRUE requires close confirmation.
 *
 * Set confirmation policy for close operations.
 */
void lds_terminal_settings_set_confirm_close(gboolean confirm);
/**
 * lds_terminal_settings_set_confirm_running_process:
 * @confirm: %TRUE requires confirmation when running jobs exist.
 *
 * Set running-job close confirmation policy.
 */
void lds_terminal_settings_set_confirm_running_process(gboolean confirm);
/**
 * lds_terminal_settings_set_disable_alt:
 * @disable: %TRUE disables Alt mnemonics.
 *
 * Enable or disable Alt shortcut handling.
 */
void lds_terminal_settings_set_disable_alt(gboolean disable);
/**
 * lds_terminal_settings_set_tab_position:
 * @position: GtkPositionType value.
 *
 * Set tab bar position.
 */
void lds_terminal_settings_set_tab_position(gint position);
/**
 * lds_terminal_settings_set_hide_menu_bar:
 * @hide: %TRUE hides menu bar.
 *
 * Set menu visibility policy.
 */
void lds_terminal_settings_set_hide_menu_bar(gboolean hide);
/**
 * lds_terminal_settings_set_strict_determinism:
 * @enabled: %TRUE enables strict determinism behavior.
 *
 * Set strict determinism runtime mode.
 */
void lds_terminal_settings_set_strict_determinism(gboolean enabled);

/**
 * lds_terminal_settings_hide_menu_bar:
 *
 * Return whether menu bar is hidden.
 *
 * Returns: %TRUE when hidden.
 */
gboolean lds_terminal_settings_hide_menu_bar(void);

/**
 * lds_terminal_settings_strict_determinism:
 *
 * Return Strict Determinism mode state.
 *
 * Returns: %TRUE when enabled.
 */
gboolean lds_terminal_settings_strict_determinism(void);
/**
 * lds_terminal_settings_tab_position:
 *
 * Return tab position.
 *
 * Returns: GtkPositionType value.
 */
gint lds_terminal_settings_tab_position(void);

/**
 * lds_terminal_settings_window_width:
 *
 * Return persisted window width.
 *
 * Returns: Width in pixels.
 */
gint lds_terminal_settings_window_width(void);
/**
 * lds_terminal_settings_window_height:
 *
 * Return persisted window height.
 *
 * Returns: Height in pixels.
 */
gint lds_terminal_settings_window_height(void);
/**
 * lds_terminal_settings_window_x:
 *
 * Return persisted window X position.
 *
 * Returns: X coordinate in pixels.
 */
gint lds_terminal_settings_window_x(void);
/**
 * lds_terminal_settings_window_y:
 *
 * Return persisted window Y position.
 *
 * Returns: Y coordinate in pixels.
 */
gint lds_terminal_settings_window_y(void);

/**
 * lds_terminal_settings_set_window_geometry:
 * @width: Window width.
 * @height: Window height.
 * @x: Window X position.
 * @y: Window Y position.
 *
 * Store window geometry values.
 */
void lds_terminal_settings_set_window_geometry(gint width, gint height, gint x, gint y);

/**
 * lds_terminal_settings_background_transparent:
 *
 * Return whether background is transparent.
 *
 * Returns: %TRUE when transparent.
 */
gboolean lds_terminal_settings_background_transparent(void);

/**
 * lds_terminal_settings_apply:
 * @terminal: (not nullable): Terminal instance.
 *
 * Apply settings to a terminal window.
 */
void lds_terminal_settings_apply(LdsTerminal *terminal);
/**
 * lds_terminal_settings_apply_ui_only:
 * @terminal: (not nullable): Terminal instance.
 *
 * Apply only UI/global settings (style, menu visibility, tab bar position).
 */
void lds_terminal_settings_apply_ui_only(LdsTerminal *terminal);
/**
 * lds_terminal_settings_apply_current:
 * @terminal: (not nullable): Terminal instance.
 *
 * Apply settings only to the active/current VTE in this window.
 */
void lds_terminal_settings_apply_current(LdsTerminal *terminal);
/**
 * lds_terminal_settings_apply_vte_current:
 * @terminal: (not nullable): Terminal instance.
 *
 * Apply VTE settings only to the active/current terminal pane.
 */
void lds_terminal_settings_apply_vte_current(LdsTerminal *terminal);
/**
 * lds_terminal_settings_apply_vte_all:
 * @terminal: (not nullable): Terminal instance.
 *
 * Apply VTE settings to all terminal panes in this window.
 */
void lds_terminal_settings_apply_vte_all(LdsTerminal *terminal);
/**
 * lds_terminal_settings_apply_to_all:
 * @terminal: (not nullable): Any terminal instance.
 *
 * Apply settings to all windows.
 */
void lds_terminal_settings_apply_to_all(LdsTerminal *terminal);

#endif /* SETTINGS_H */
