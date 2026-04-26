/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Spawn-close lifecycle regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"

static LdsTerminal *test_terminal_new(void) {
	LdsTerminal *terminal = g_new0(LdsTerminal, 1);
	terminal->terms = g_ptr_array_new();
	return terminal;
}

static LdsTerminalTerm *test_term_new(LdsTerminal *terminal) {
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);
	term->parent = terminal;
	term->pid = -1;
	return term;
}

static void test_terminal_free(LdsTerminal *terminal) {
	if (!terminal)
		return;

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		g_free(term);
	}

	g_ptr_array_free(terminal->terms, TRUE);
	g_free(terminal);
}

static void test_spawn_ready_success_while_closing_removes_term(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *closing = test_term_new(terminal);
	LdsTerminalTerm *remaining = test_term_new(terminal);

	closing->closing = TRUE;

	g_ptr_array_add(terminal->terms, closing);
	g_ptr_array_add(terminal->terms, remaining);

	lds_terminal_vte_handle_spawn_ready(closing, 0, NULL);

	g_assert_cmpuint(terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == remaining);

	test_terminal_free(terminal);
}

static void test_spawn_ready_error_while_closing_removes_term(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *closing = test_term_new(terminal);
	LdsTerminalTerm *remaining = test_term_new(terminal);
	GError *error = NULL;

	closing->closing = TRUE;

	g_ptr_array_add(terminal->terms, closing);
	g_ptr_array_add(terminal->terms, remaining);

	error = g_error_new_literal(g_quark_from_static_string("test"), 1, "spawn failure");
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Failed to spawn terminal child*");
	lds_terminal_vte_handle_spawn_ready(closing, 0, error);
	g_test_assert_expected_messages();
	g_error_free(error);

	g_assert_cmpuint(terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == remaining);

	test_terminal_free(terminal);
}

static void test_spawn_ready_success_not_closing_keeps_term_and_sets_pid(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *term = test_term_new(terminal);
	const GPid expected_pid = 4242;

	term->closing = FALSE;

	g_ptr_array_add(terminal->terms, term);

	lds_terminal_vte_handle_spawn_ready(term, expected_pid, NULL);

	g_assert_cmpuint(terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == term);
	g_assert_cmpint(term->pid, ==, (gint)expected_pid);

	test_terminal_free(terminal);
}

static void test_spawn_ready_is_noop_for_foreign_term(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *foreign = test_term_new(terminal);

	/* Foreign term is not tracked by terminal->terms. */
	lds_terminal_vte_handle_spawn_ready(foreign, 0, NULL);

	g_assert_cmpuint(terminal->terms->len, ==, 0);
	g_assert_false(foreign->spawn_failed);
	g_assert_cmpint(foreign->pid, ==, -1);

	g_free(foreign);
	test_terminal_free(terminal);
}

static void test_spawn_ready_is_noop_when_terminal_destroyed(void) {
	LdsTerminal *terminal = test_terminal_new();
	LdsTerminalTerm *term = test_term_new(terminal);

	g_ptr_array_add(terminal->terms, term);
	terminal->destroyed = TRUE;

	lds_terminal_vte_handle_spawn_ready(term, 0, NULL);

	g_assert_cmpuint(terminal->terms->len, ==, 1);
	g_assert_true(g_ptr_array_index(terminal->terms, 0) == term);
	g_assert_false(term->spawn_failed);
	g_assert_cmpint(term->pid, ==, -1);

	test_terminal_free(terminal);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/lifecycle/spawn-ready-success-while-closing-removes-term",
					test_spawn_ready_success_while_closing_removes_term);
	g_test_add_func("/lifecycle/spawn-ready-error-while-closing-removes-term",
					test_spawn_ready_error_while_closing_removes_term);
	g_test_add_func("/lifecycle/spawn-ready-success-not-closing-keeps-term-and-sets-pid",
					test_spawn_ready_success_not_closing_keeps_term_and_sets_pid);
	g_test_add_func("/lifecycle/spawn-ready-noop-foreign-term",
					test_spawn_ready_is_noop_for_foreign_term);
	g_test_add_func("/lifecycle/spawn-ready-noop-when-terminal-destroyed",
					test_spawn_ready_is_noop_when_terminal_destroyed);

	return g_test_run();
}
