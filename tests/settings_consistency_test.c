/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Settings consistency regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>

#include "settings.h"

static void test_scrollback_is_clamped_to_bounds(void) {
	lds_terminal_settings_reset_defaults();

	lds_terminal_settings_set_scrollback(1);
	g_assert_cmpuint(lds_terminal_settings_scrollback(), ==, 1000);

	lds_terminal_settings_set_scrollback(999999);
	g_assert_cmpuint(lds_terminal_settings_scrollback(), ==, 50000);
}

static void test_font_name_null_falls_back_to_default(void) {
	lds_terminal_settings_reset_defaults();

	lds_terminal_settings_set_font_name(NULL);
	g_assert_cmpstr(lds_terminal_settings_font_name(), ==, "Monospace 10");
}

static void test_window_geometry_updates_size_and_position(void) {
	lds_terminal_settings_reset_defaults();

	lds_terminal_settings_set_window_geometry(1024, 768, 40, 60);

	g_assert_cmpint(lds_terminal_settings_window_width(), ==, 1024);
	g_assert_cmpint(lds_terminal_settings_window_height(), ==, 768);
	g_assert_cmpint(lds_terminal_settings_window_x(), ==, 40);
	g_assert_cmpint(lds_terminal_settings_window_y(), ==, 60);
}

static void test_theme_mode_roundtrip_and_legacy_wrapper(void) {
	lds_terminal_settings_reset_defaults();

	lds_terminal_settings_set_theme_mode(LDS_TERMINAL_THEME_DARK);
	g_assert_cmpint(lds_terminal_settings_theme_mode(), ==, LDS_TERMINAL_THEME_DARK);
	g_assert_false(lds_terminal_settings_follow_system_theme());

	lds_terminal_settings_set_theme_mode(LDS_TERMINAL_THEME_LIGHT);
	g_assert_cmpint(lds_terminal_settings_theme_mode(), ==, LDS_TERMINAL_THEME_LIGHT);
	g_assert_false(lds_terminal_settings_follow_system_theme());

	lds_terminal_settings_set_follow_system_theme(TRUE);
	g_assert_cmpint(lds_terminal_settings_theme_mode(), ==, LDS_TERMINAL_THEME_SYSTEM);
	g_assert_true(lds_terminal_settings_follow_system_theme());
}

static void test_sync_prompt_colors_roundtrip(void) {
	lds_terminal_settings_reset_defaults();

	g_assert_true(lds_terminal_settings_sync_prompt_colors());
	lds_terminal_settings_set_sync_prompt_colors(FALSE);
	g_assert_false(lds_terminal_settings_sync_prompt_colors());
	lds_terminal_settings_set_sync_prompt_colors(TRUE);
	g_assert_true(lds_terminal_settings_sync_prompt_colors());
}

static void test_confirm_running_process_roundtrip(void) {
	lds_terminal_settings_reset_defaults();

	g_assert_true(lds_terminal_settings_confirm_running_process_enabled());
	lds_terminal_settings_set_confirm_running_process(FALSE);
	g_assert_false(lds_terminal_settings_confirm_running_process_enabled());
	lds_terminal_settings_set_confirm_running_process(TRUE);
	g_assert_true(lds_terminal_settings_confirm_running_process_enabled());
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/settings/scrollback/is-clamped-to-bounds",
					test_scrollback_is_clamped_to_bounds);
	g_test_add_func("/settings/font-name/null-falls-back-to-default",
					test_font_name_null_falls_back_to_default);
	g_test_add_func("/settings/window-geometry/updates-size-and-position",
					test_window_geometry_updates_size_and_position);
	g_test_add_func("/settings/theme-mode/roundtrip-and-legacy-wrapper",
					test_theme_mode_roundtrip_and_legacy_wrapper);
	g_test_add_func("/settings/sync-prompt-colors/roundtrip", test_sync_prompt_colors_roundtrip);
	g_test_add_func("/settings/confirm-running-process/roundtrip",
					test_confirm_running_process_roundtrip);

	return g_test_run();
}
