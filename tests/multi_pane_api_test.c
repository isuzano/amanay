/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Multi-pane API regression coverage.
 */

#include <glib.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"

static void test_multi_pane_api_null_safe(void) {
	g_assert_false(lds_terminal_vte_close_active_pane(NULL));
	g_assert_false(lds_terminal_vte_active_has_selection(NULL));
	g_assert_false(lds_terminal_vte_active_clipboard_has_text(NULL));
	g_assert_false(lds_terminal_vte_has_split(NULL));
}

static void test_multi_pane_api_split_flag_reflects_secondary_pointer(void) {
	LdsTerminalTerm term = {0};

	g_assert_false(lds_terminal_vte_has_split(&term));

	term.secondary_vte = (GtkWidget *)0x1;
	g_assert_true(lds_terminal_vte_has_split(&term));
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/multi-pane/api/null-safe", test_multi_pane_api_null_safe);
	g_test_add_func("/multi-pane/api/split-flag-reflects-secondary-pointer",
					test_multi_pane_api_split_flag_reflects_secondary_pointer);

	return g_test_run();
}
