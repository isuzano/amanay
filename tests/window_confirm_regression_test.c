/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Window close confirmation regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>

#include "lds_terminal.h"
#include "settings.h"
#include "internal/lds_terminal_internal.h"

static LdsTerminal *test_terminal_new(guint tabs_len, gboolean has_window) {
	LdsTerminal *terminal = g_new0(LdsTerminal, 1);
	terminal->terms = g_ptr_array_new();
	terminal->window = has_window ? (GtkWidget *)GINT_TO_POINTER(1) : NULL;

	for (guint i = 0; i < tabs_len; i++)
		g_ptr_array_add(terminal->terms, GINT_TO_POINTER(1));

	return terminal;
}

static void test_terminal_free(LdsTerminal *terminal) {
	if (!terminal)
		return;

	g_ptr_array_free(terminal->terms, TRUE);
	g_free(terminal);
}

static void test_confirm_close_requires_multiple_tabs_and_enabled_setting(void) {
	LdsTerminal *terminal = test_terminal_new(2, TRUE);

	lds_terminal_settings_set_confirm_close(TRUE);
	if (!lds_terminal_settings_confirm_close_enabled()) {
		g_test_skip("Confirm-close setting cannot be enabled in current settings backend");
		test_terminal_free(terminal);
		return;
	}
	g_assert_cmpuint(terminal->terms->len, ==, 2u);
	g_assert_nonnull(terminal->window);
	g_assert_true(lds_terminal_window_should_confirm_close(terminal));

	test_terminal_free(terminal);
}

static void test_confirm_close_disabled_setting_skips_confirmation(void) {
	LdsTerminal *terminal = test_terminal_new(2, TRUE);

	lds_terminal_settings_set_confirm_close(FALSE);
	g_assert_false(lds_terminal_window_should_confirm_close(terminal));

	test_terminal_free(terminal);
	lds_terminal_settings_set_confirm_close(TRUE);
}

static void test_confirm_close_single_tab_skips_confirmation(void) {
	LdsTerminal *terminal = test_terminal_new(1, TRUE);

	lds_terminal_settings_set_confirm_close(TRUE);
	g_assert_false(lds_terminal_window_should_confirm_close(terminal));

	test_terminal_free(terminal);
}

static void test_confirm_close_without_window_skips_confirmation(void) {
	LdsTerminal *terminal = test_terminal_new(2, FALSE);

	lds_terminal_settings_set_confirm_close(TRUE);
	if (!lds_terminal_settings_confirm_close_enabled()) {
		g_test_skip("Confirm-close setting cannot be enabled in current settings backend");
		test_terminal_free(terminal);
		return;
	}
	g_assert_false(lds_terminal_window_should_confirm_close(terminal));

	test_terminal_free(terminal);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/window/confirm/requires-multiple-tabs-and-enabled-setting",
					test_confirm_close_requires_multiple_tabs_and_enabled_setting);
	g_test_add_func("/window/confirm/disabled-setting-skips-confirmation",
					test_confirm_close_disabled_setting_skips_confirmation);
	g_test_add_func("/window/confirm/single-tab-skips-confirmation",
					test_confirm_close_single_tab_skips_confirmation);
	g_test_add_func("/window/confirm/without-window-skips-confirmation",
					test_confirm_close_without_window_skips_confirmation);

	return g_test_run();
}
