/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Concurrent close lifecycle regression coverage.
 */

#include <glib.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"

static LdsTerminal *test_terminal_new(void) {
	LdsTerminal *terminal = g_new0(LdsTerminal, 1);
	terminal->terms = g_ptr_array_new();
	terminal->window = NULL;
	return terminal;
}

static LdsTerminalTerm *test_term_new(LdsTerminal *terminal) {
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);
	term->parent = terminal;
	term->pid = -1;
	term->page = NULL;
	term->closing = FALSE;
	term->spawn_failed = FALSE;
	return term;
}

static void test_terminal_free(LdsTerminal *terminal) {
	if (!terminal)
		return;

	while (terminal->terms->len > 0) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, terminal->terms->len - 1);
		g_ptr_array_remove_index(terminal->terms, terminal->terms->len - 1);
		g_free(term);
	}

	g_ptr_array_free(terminal->terms, TRUE);
	g_free(terminal);
}

static void test_close_multiple_terms_without_page_is_safe(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *a = test_term_new(terminal);
	LdsTerminalTerm *b = test_term_new(terminal);

	g_ptr_array_add(terminal->terms, a);
	g_ptr_array_add(terminal->terms, b);

	lds_terminal_tabs_close(terminal, a, LDS_TERMINAL_TABS_CLOSE_BY_USER);
	lds_terminal_tabs_close(terminal, b, LDS_TERMINAL_TABS_CLOSE_BY_USER);

	g_assert_cmpuint(terminal->terms->len, ==, 0);

	test_terminal_free(terminal);
}

static void test_close_without_page_removes_term_for_shutdown_reason(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *term = test_term_new(terminal);

	g_ptr_array_add(terminal->terms, term);

	lds_terminal_tabs_close(terminal, term, LDS_TERMINAL_TABS_CLOSE_BY_SHUTDOWN);

	g_assert_cmpuint(terminal->terms->len, ==, 0);

	test_terminal_free(terminal);
}

static void test_close_ignores_term_not_owned_by_terminal(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminal *foreign_terminal = test_terminal_new();
	LdsTerminalTerm *owned = test_term_new(terminal);
	LdsTerminalTerm *foreign = test_term_new(foreign_terminal);

	g_ptr_array_add(terminal->terms, owned);
	g_ptr_array_add(foreign_terminal->terms, foreign);

	lds_terminal_tabs_close(terminal, foreign, LDS_TERMINAL_TABS_CLOSE_BY_USER);

	g_assert_cmpuint(terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == owned);
	g_assert_cmpuint(foreign_terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(foreign_terminal->terms, 0) == foreign);
	g_assert_false(foreign->closing);

	test_terminal_free(terminal);
	test_terminal_free(foreign_terminal);
}

static void test_close_is_noop_when_terminal_is_destroyed(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *term = test_term_new(terminal);

	g_ptr_array_add(terminal->terms, term);
	terminal->destroyed = TRUE;

	lds_terminal_tabs_close(terminal, term, LDS_TERMINAL_TABS_CLOSE_BY_USER);

	g_assert_cmpuint(terminal->terms->len, ==, 1u);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == term);
	g_assert_false(term->closing);

	test_terminal_free(terminal);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/tabs/close-concurrency/multiple-terms-without-page-safe",
					test_close_multiple_terms_without_page_is_safe);
	g_test_add_func("/tabs/close-concurrency/shutdown-without-page-removes-term",
					test_close_without_page_removes_term_for_shutdown_reason);
	g_test_add_func("/tabs/close-concurrency/ignores-foreign-term",
					test_close_ignores_term_not_owned_by_terminal);
	g_test_add_func("/tabs/close-concurrency/noop-when-terminal-destroyed",
					test_close_is_noop_when_terminal_is_destroyed);

	return g_test_run();
}
