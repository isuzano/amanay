/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Search policy regression coverage.
 */

#include <glib.h>

#include "internal/lds_terminal_internal.h"

static void test_debounce_low_scrollback(void) {
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(FALSE, 5000), ==, 150u);
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(TRUE, 5000), ==, 350u);
}

static void test_debounce_medium_scrollback(void) {
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(FALSE, 10000), ==, 220u);
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(TRUE, 10000), ==, 500u);
}

static void test_debounce_high_scrollback(void) {
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(FALSE, 30000), ==, 300u);
	g_assert_cmpuint(lds_terminal_search_compute_debounce_ms(TRUE, 30000), ==, 700u);
}

static void test_search_count_plain_text_case_sensitive(void) {
	gboolean valid_regex = FALSE;
	guint count = lds_terminal_search_count_matches_for_query("foo bar foo", "foo", TRUE, FALSE,
															  FALSE, &valid_regex);
	g_assert_true(valid_regex);
	g_assert_cmpuint(count, ==, 2u);
}

static void test_search_count_plain_text_whole_word(void) {
	gboolean valid_regex = FALSE;
	guint count = lds_terminal_search_count_matches_for_query("foobar foo", "foo", TRUE, FALSE,
															  TRUE, &valid_regex);
	g_assert_true(valid_regex);
	g_assert_cmpuint(count, ==, 1u);
}

static void test_search_count_regex_case_insensitive(void) {
	gboolean valid_regex = FALSE;
	guint count = lds_terminal_search_count_matches_for_query("Foo fOo fzz", "f.o", FALSE, TRUE,
															  FALSE, &valid_regex);
	g_assert_true(valid_regex);
	g_assert_cmpuint(count, ==, 2u);
}

static void test_search_count_invalid_regex_returns_zero(void) {
	gboolean valid_regex = TRUE;
	guint count =
		lds_terminal_search_count_matches_for_query("foo", "(", TRUE, TRUE, FALSE, &valid_regex);
	g_assert_false(valid_regex);
	g_assert_cmpuint(count, ==, 0u);
}

static void test_search_count_with_regex_limit_truncates(void) {
	gboolean valid_regex = FALSE;
	g_autoptr(GRegex) regex = NULL;
	g_assert_true(lds_terminal_search_build_regex("foo", TRUE, FALSE, FALSE, &regex, &valid_regex));
	g_assert_true(valid_regex);
	g_assert_nonnull(regex);

	gboolean truncated = FALSE;
	guint count = lds_terminal_search_count_matches_with_regex("foo foo foo foo foo", regex, 3u,
															   &truncated, NULL);
	g_assert_true(truncated);
	g_assert_cmpuint(count, ==, 3u);
}

static void test_search_count_with_regex_honors_cancellation(void) {
	gboolean valid_regex = FALSE;
	g_autoptr(GRegex) regex = NULL;
	g_assert_true(lds_terminal_search_build_regex("a", TRUE, FALSE, FALSE, &regex, &valid_regex));
	g_assert_true(valid_regex);
	g_assert_nonnull(regex);

	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_cancellable_cancel(cancellable);

	gboolean truncated = FALSE;
	guint count = lds_terminal_search_count_matches_with_regex(
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", regex,
		0u, &truncated, cancellable);
	g_assert_false(truncated);
	g_assert_cmpuint(count, <=, 32u);
}

static void test_search_duplicate_running_active_request(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm term = {0};

	terminal.search_count_running = TRUE;
	terminal.search_haystack_generation = 7u;
	terminal.search_count_active_term = &term;
	terminal.search_count_active_query = g_strdup("needle");
	terminal.search_count_active_opt_flags = 3u;
	terminal.search_count_active_generation = 7u;

	g_assert_true(lds_terminal_search_count_request_is_duplicate(&terminal, &term, "needle", 3u));
	g_assert_false(lds_terminal_search_count_request_is_duplicate(&terminal, &term, "needle", 4u));
	g_assert_false(lds_terminal_search_count_request_is_duplicate(&terminal, &term, "other", 3u));

	g_free(terminal.search_count_active_query);
}

static void test_search_duplicate_running_pending_request(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm term = {0};

	terminal.search_count_running = TRUE;
	terminal.search_count_reschedule = TRUE;
	terminal.search_haystack_generation = 11u;
	terminal.search_pending_term = &term;
	terminal.search_pending_query = g_strdup("foo");
	terminal.search_pending_opt_flags = 9u;
	terminal.search_pending_generation = 11u;

	g_assert_true(lds_terminal_search_count_request_is_duplicate(&terminal, &term, "foo", 9u));
	terminal.search_pending_generation = 10u;
	g_assert_false(lds_terminal_search_count_request_is_duplicate(&terminal, &term, "foo", 9u));

	g_free(terminal.search_pending_query);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/search/policy/debounce-low-scrollback", test_debounce_low_scrollback);
	g_test_add_func("/search/policy/debounce-medium-scrollback", test_debounce_medium_scrollback);
	g_test_add_func("/search/policy/debounce-high-scrollback", test_debounce_high_scrollback);
	g_test_add_func("/search/policy/count-plain-text-case-sensitive",
					test_search_count_plain_text_case_sensitive);
	g_test_add_func("/search/policy/count-plain-text-whole-word",
					test_search_count_plain_text_whole_word);
	g_test_add_func("/search/policy/count-regex-case-insensitive",
					test_search_count_regex_case_insensitive);
	g_test_add_func("/search/policy/count-invalid-regex-returns-zero",
					test_search_count_invalid_regex_returns_zero);
	g_test_add_func("/search/policy/count-regex-limit-truncates",
					test_search_count_with_regex_limit_truncates);
	g_test_add_func("/search/policy/count-regex-cancellable",
					test_search_count_with_regex_honors_cancellation);
	g_test_add_func("/search/policy/duplicate-running-active-request",
					test_search_duplicate_running_active_request);
	g_test_add_func("/search/policy/duplicate-running-pending-request",
					test_search_duplicate_running_pending_request);

	return g_test_run();
}
