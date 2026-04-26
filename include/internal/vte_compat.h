/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE compatibility helpers.
 */

#ifndef LDS_TERMINAL_VTE_COMPAT_H
#define LDS_TERMINAL_VTE_COMPAT_H

#include <vte/vte.h>

#if !VTE_CHECK_VERSION(0, 46, 0)
#error "Amanay requires VTE >= 0.46"
#endif

static inline GtkWidget *lds_terminal_vte_terminal_new(void) {
	return vte_terminal_new();
}

static inline void lds_terminal_vte_terminal_set_backspace_binding(VteTerminal *terminal,
																   VteEraseBinding binding) {
	vte_terminal_set_backspace_binding(terminal, binding);
}

static inline void lds_terminal_vte_terminal_set_delete_binding(VteTerminal *terminal,
																VteEraseBinding binding) {
	vte_terminal_set_delete_binding(terminal, binding);
}

static inline void lds_terminal_vte_terminal_spawn_async(
	VteTerminal *terminal, VtePtyFlags pty_flags, const char *working_directory, char **argv,
	char **envv, GSpawnFlags spawn_flags, GSpawnChildSetupFunc child_setup,
	gpointer child_setup_data, GDestroyNotify child_setup_data_destroy, int timeout,
	GCancellable *cancellable, VteTerminalSpawnAsyncCallback callback, gpointer user_data) {
	vte_terminal_spawn_async(terminal, pty_flags, working_directory, argv, envv, spawn_flags,
							 child_setup, child_setup_data, child_setup_data_destroy, timeout,
							 cancellable, callback, user_data);
}

static inline void lds_terminal_vte_terminal_fork_command_full(
	VteTerminal *terminal, VtePtyFlags pty_flags, const char *working_directory, char **argv,
	char **envv, GSpawnFlags spawn_flags, GSpawnChildSetupFunc child_setup,
	gpointer child_setup_data, GPid *child_pid, GError **error) {
#if !VTE_CHECK_VERSION(0, 38, 0)
	vte_terminal_fork_command_full(terminal, pty_flags, working_directory, argv, envv, spawn_flags,
								   child_setup, child_setup_data, child_pid, error);
#else
	(void)terminal;
	(void)pty_flags;
	(void)working_directory;
	(void)argv;
	(void)envv;
	(void)spawn_flags;
	(void)child_setup;
	(void)child_setup_data;
	(void)child_pid;
	if (error)
		*error = NULL;
#endif
}

static inline void lds_terminal_vte_terminal_feed(VteTerminal *terminal, const char *data,
												  gssize length) {
	vte_terminal_feed(terminal, data, length);
}

static inline void lds_terminal_vte_terminal_set_font(VteTerminal *terminal,
													  const PangoFontDescription *font_desc) {
	vte_terminal_set_font(terminal, font_desc);
}

static inline void lds_terminal_vte_terminal_set_font_from_string(VteTerminal *terminal,
																  const char *font_desc) {
#if !VTE_CHECK_VERSION(0, 38, 0)
	vte_terminal_set_font_from_string(terminal, font_desc);
#else
	(void)terminal;
	(void)font_desc;
#endif
}

static inline void lds_terminal_vte_terminal_set_scrollback_lines(VteTerminal *terminal,
																  glong lines) {
	vte_terminal_set_scrollback_lines(terminal, lines);
}

static inline void lds_terminal_vte_terminal_set_cursor_blink_mode(VteTerminal *terminal,
																   VteCursorBlinkMode mode) {
	vte_terminal_set_cursor_blink_mode(terminal, mode);
}

static inline void lds_terminal_vte_terminal_set_cursor_shape(VteTerminal *terminal,
															  VteCursorShape shape) {
	vte_terminal_set_cursor_shape(terminal, shape);
}

static inline void lds_terminal_vte_terminal_copy_clipboard_format(VteTerminal *terminal,
																   VteFormat format) {
	vte_terminal_copy_clipboard_format(terminal, format);
}

static inline void lds_terminal_vte_terminal_paste_clipboard(VteTerminal *terminal) {
	vte_terminal_paste_clipboard(terminal);
}

static inline void lds_terminal_vte_terminal_search_set_regex(VteTerminal *terminal,
															  VteRegex *regex, guint32 flags) {
	vte_terminal_search_set_regex(terminal, regex, flags);
}

static inline void lds_terminal_vte_terminal_search_set_wrap_around(VteTerminal *terminal,
																	gboolean wrap) {
	vte_terminal_search_set_wrap_around(terminal, wrap);
}

static inline gboolean lds_terminal_vte_terminal_search_find_next(VteTerminal *terminal) {
	return vte_terminal_search_find_next(terminal);
}

static inline gboolean lds_terminal_vte_terminal_search_find_previous(VteTerminal *terminal) {
	return vte_terminal_search_find_previous(terminal);
}

static inline gchar *lds_terminal_vte_terminal_match_check_at(VteTerminal *terminal, double x,
															  double y, int *tag) {
#if _VTE_GTK == 4
	return vte_terminal_check_match_at(terminal, x, y, tag);
#else
	return vte_terminal_match_check(terminal, (glong)x, (glong)y, tag);
#endif
}

static inline gchar *lds_terminal_vte_terminal_hyperlink_check_at(VteTerminal *terminal, double x,
																  double y) {
#if _VTE_GTK == 4
	return vte_terminal_check_hyperlink_at(terminal, x, y);
#else
	(void)terminal;
	(void)x;
	(void)y;
	return NULL;
#endif
}

static inline int lds_terminal_vte_terminal_match_add_regex(VteTerminal *terminal, VteRegex *regex,
															guint32 flags) {
	return vte_terminal_match_add_regex(terminal, regex, flags);
}

static inline void lds_terminal_vte_terminal_match_set_cursor_name(VteTerminal *terminal, int tag,
																   const char *cursor_name) {
	vte_terminal_match_set_cursor_name(terminal, tag, cursor_name);
}

static inline VteRegex *lds_terminal_vte_regex_new_for_match(const char *pattern, gssize length,
															 guint32 flags, GError **error) {
	return vte_regex_new_for_match(pattern, length, flags, error);
}

static inline VteRegex *lds_terminal_vte_regex_new_for_search(const char *pattern, gssize length,
															  guint32 flags, GError **error) {
	return vte_regex_new_for_search(pattern, length, flags, error);
}

static inline void lds_terminal_vte_regex_unref(VteRegex *regex) {
	vte_regex_unref(regex);
}

static inline gchar *lds_terminal_vte_terminal_get_text_range_format(VteTerminal *terminal,
																	 VteFormat format,
																	 glong start_row,
																	 glong start_col, glong end_row,
																	 glong end_col, gsize *length) {
	return vte_terminal_get_text_range_format(terminal, format, start_row, start_col, end_row,
											  end_col, length);
}

static inline glong lds_terminal_vte_terminal_get_char_width(VteTerminal *terminal) {
	return vte_terminal_get_char_width(terminal);
}

static inline glong lds_terminal_vte_terminal_get_char_height(VteTerminal *terminal) {
	return vte_terminal_get_char_height(terminal);
}

static inline glong lds_terminal_vte_terminal_get_row_count(VteTerminal *terminal) {
	return vte_terminal_get_row_count(terminal);
}

static inline glong lds_terminal_vte_terminal_get_column_count(VteTerminal *terminal) {
	return vte_terminal_get_column_count(terminal);
}

static inline gboolean lds_terminal_vte_terminal_get_scroll_unit_is_pixels(VteTerminal *terminal) {
	return vte_terminal_get_scroll_unit_is_pixels(terminal);
}

#endif
