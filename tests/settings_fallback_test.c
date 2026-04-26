/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Settings fallback regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>

#include "settings.h"

static void test_missing_schema_uses_defaults_and_null_backend(void) {
	GSettings *backend = lds_terminal_settings_backend();
	g_assert_null(backend);

	g_assert_cmpstr(lds_terminal_settings_font_name(), ==, "Monospace 10");

	lds_terminal_settings_set_scrollback(999999);
	g_assert_cmpuint(lds_terminal_settings_scrollback(), ==, 50000);

	lds_terminal_settings_set_window_geometry(900, 700, 10, 20);
	g_assert_cmpint(lds_terminal_settings_window_width(), ==, 900);
	g_assert_cmpint(lds_terminal_settings_window_height(), ==, 700);
	g_assert_cmpint(lds_terminal_settings_window_x(), ==, 10);
	g_assert_cmpint(lds_terminal_settings_window_y(), ==, 20);

	lds_terminal_settings_set_font_name("Monospace 17");
	lds_terminal_settings_set_sync_prompt_colors(FALSE);
	lds_terminal_settings_reset_defaults();

	g_assert_cmpstr(lds_terminal_settings_font_name(), ==, "Monospace 10");
	g_assert_cmpuint(lds_terminal_settings_scrollback(), ==, 10000);
	g_assert_true(lds_terminal_settings_sync_prompt_colors());
	g_assert_cmpint(lds_terminal_settings_window_width(), ==, 800);
	g_assert_cmpint(lds_terminal_settings_window_height(), ==, 500);
	g_assert_cmpint(lds_terminal_settings_window_x(), ==, -1);
	g_assert_cmpint(lds_terminal_settings_window_y(), ==, -1);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/settings/fallback/missing-schema-uses-defaults-and-null-backend",
					test_missing_schema_uses_defaults_and_null_backend);

	return g_test_run();
}
