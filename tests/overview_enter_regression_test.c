/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Overview Enter key regression coverage.
 */

#include <glib.h>
#include <gtk/gtk.h>

#include "internal/lds_terminal_internal.h"

static void test_enter_is_consumed_when_search_is_active(void) {
	g_assert_true(lds_terminal_overview_should_consume_enter(GDK_KEY_Return, TRUE));
	g_assert_true(lds_terminal_overview_should_consume_enter(GDK_KEY_KP_Enter, TRUE));
}

static void test_enter_is_not_consumed_when_search_is_inactive(void) {
	g_assert_false(lds_terminal_overview_should_consume_enter(GDK_KEY_Return, FALSE));
	g_assert_false(lds_terminal_overview_should_consume_enter(GDK_KEY_KP_Enter, FALSE));
}

static void test_non_enter_is_not_consumed_even_with_search_active(void) {
	g_assert_false(lds_terminal_overview_should_consume_enter(GDK_KEY_space, TRUE));
	g_assert_false(lds_terminal_overview_should_consume_enter(GDK_KEY_a, TRUE));
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/overview/enter/consumed-when-search-active",
					test_enter_is_consumed_when_search_is_active);
	g_test_add_func("/overview/enter/not-consumed-when-search-inactive",
					test_enter_is_not_consumed_when_search_is_inactive);
	g_test_add_func("/overview/enter/non-enter-not-consumed",
					test_non_enter_is_not_consumed_even_with_search_active);

	return g_test_run();
}
