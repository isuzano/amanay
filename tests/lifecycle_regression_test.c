/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal lifecycle regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>

#include "lds_terminal.h"
#include "menu.h"
#include "internal/lds_terminal_internal.h"

static gboolean test_timeout_cb(gpointer data) {
	(void)data;
	return G_SOURCE_REMOVE;
}

static void test_initial_term_validation_handles_null_without_crash(void) {
	LdsTerminal terminal = {0};
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Failed to create initial terminal tab*");
	g_assert_false(lds_terminal_validate_initial_term(&terminal, NULL));
	g_test_assert_expected_messages();
}

static void test_initial_term_validation_accepts_valid_term(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm term = {0};
	g_assert_true(lds_terminal_validate_initial_term(&terminal, &term));
}

static void test_context_model_lifecycle_is_stable(void) {
	GMenuModel *a = lds_terminal_menu_build_context_model();
	GMenuModel *b = lds_terminal_menu_build_context_model();
	GMenuModel *c = lds_terminal_menu_build_context_model();

	g_assert_nonnull(a);
	g_assert_nonnull(b);
	g_assert_nonnull(c);
	g_assert_true(a == b);
	g_assert_true(b == c);

	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(c);

	GMenuModel *d = lds_terminal_menu_build_context_model();
	g_assert_nonnull(d);
	g_object_unref(d);
}

static gboolean test_menu_model_has_action_recursive(GMenuModel *model, const char *action_name) {
	if (!model || !action_name)
		return FALSE;

	gint n = g_menu_model_get_n_items(model);
	for (gint i = 0; i < n; i++) {
		g_autoptr(GVariant) action_v = g_menu_model_get_item_attribute_value(
			model, i, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING);
		if (action_v) {
			const char *action = g_variant_get_string(action_v, NULL);
			if (g_strcmp0(action, action_name) == 0)
				return TRUE;
		}

		GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		if (section) {
			gboolean found = test_menu_model_has_action_recursive(section, action_name);
			g_object_unref(section);
			if (found)
				return TRUE;
		}

		GMenuModel *submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
		if (submenu) {
			gboolean found = test_menu_model_has_action_recursive(submenu, action_name);
			g_object_unref(submenu);
			if (found)
				return TRUE;
		}
	}

	return FALSE;
}

static void test_context_model_contains_link_actions(void) {
	g_autoptr(GMenuModel) model = lds_terminal_menu_build_context_model();
	g_assert_nonnull(model);

	g_assert_true(test_menu_model_has_action_recursive(model, "term.open-link"));
	g_assert_true(test_menu_model_has_action_recursive(model, "term.copy-link"));
}

static void test_cancel_search_debounce_removes_pending_source(void) {
	LdsTerminal terminal = {0};
	guint source_id = g_timeout_add_seconds(30, test_timeout_cb, NULL);
	terminal.search_state = lds_terminal_search_state_new();
	g_assert_nonnull(terminal.search_state);

	terminal.search_state->search_debounce_id = source_id;
	g_assert_nonnull(g_main_context_find_source_by_id(NULL, source_id));

	lds_terminal_cancel_search_debounce(&terminal);

	g_assert_cmpuint(terminal.search_state->search_debounce_id, ==, 0u);
	g_assert_null(g_main_context_find_source_by_id(NULL, source_id));

	/* Idempotent call should be safe. */
	lds_terminal_cancel_search_debounce(&terminal);
	g_clear_pointer(&terminal.search_state, lds_terminal_search_state_free);
}

static void test_close_selected_tab_cancels_search_debounce(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);
	guint source_id = g_timeout_add_seconds(30, test_timeout_cb, NULL);
	terminal.search_state = lds_terminal_search_state_new();
	g_assert_nonnull(terminal.search_state);

	terminal.terms = g_ptr_array_new();
	terminal.search_state->search_debounce_id = source_id;
	term->parent = &terminal;
	term->pid = -1;
	term->page = NULL;

	g_ptr_array_add(terminal.terms, term);
	g_assert_nonnull(g_main_context_find_source_by_id(NULL, source_id));

	lds_terminal_tabs_close(&terminal, term, LDS_TERMINAL_TABS_CLOSE_BY_SHUTDOWN);

	g_assert_cmpuint(terminal.search_state->search_debounce_id, ==, 0u);
	g_assert_null(g_main_context_find_source_by_id(NULL, source_id));
	g_assert_cmpuint(terminal.terms->len, ==, 0u);

	g_ptr_array_free(terminal.terms, TRUE);
	g_clear_pointer(&terminal.search_state, lds_terminal_search_state_free);
}

static void test_schedule_search_debounce_deduplicates_same_request(void) {
	LdsTerminal terminal = {0};
	terminal.search_state = lds_terminal_search_state_new();
	g_assert_nonnull(terminal.search_state);

	lds_terminal_search_schedule_debounce(&terminal, test_timeout_cb);
	guint first_id = terminal.search_state->search_debounce_id;

	g_assert_cmpuint(first_id, !=, 0u);
	g_assert_nonnull(g_main_context_find_source_by_id(NULL, first_id));

	lds_terminal_search_schedule_debounce(&terminal, test_timeout_cb);
	g_assert_cmpuint(terminal.search_state->search_debounce_id, ==, first_id);
	g_assert_nonnull(g_main_context_find_source_by_id(NULL, first_id));

	lds_terminal_cancel_search_debounce(&terminal);
	g_assert_cmpuint(terminal.search_state->search_debounce_id, ==, 0u);
	g_assert_null(g_main_context_find_source_by_id(NULL, first_id));
	g_clear_pointer(&terminal.search_state, lds_terminal_search_state_free);
}

static void test_remove_term_clears_cached_selected_state(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	terminal.terms = g_ptr_array_new();
	term->parent = &terminal;
	term->pid = -1;
	g_ptr_array_add(terminal.terms, term);

	terminal.current_selected_term = term;
	terminal.menu_edit_state_term = term;
	terminal.menu_edit_state_bits = 0xffu;
	terminal.menu_edit_state_valid = TRUE;
	terminal.search_state = lds_terminal_search_state_new();
	g_assert_nonnull(terminal.search_state);
	terminal.search_state->search_haystack_term = term;

	lds_terminal_remove_term(&terminal, term);

	g_assert_null(terminal.current_selected_term);
	g_assert_null(terminal.menu_edit_state_term);
	g_assert_cmpuint(terminal.menu_edit_state_bits, ==, 0u);
	g_assert_false(terminal.menu_edit_state_valid);
	g_assert_null(terminal.search_state->search_haystack_term);
	g_assert_cmpuint(terminal.terms->len, ==, 0u);

	g_ptr_array_free(terminal.terms, TRUE);
	g_clear_pointer(&terminal.search_state, lds_terminal_search_state_free);
}

static GSimpleActionGroup *test_context_actions_new(void) {
	GSimpleActionGroup *group = g_simple_action_group_new();
	GSimpleAction *open_link = g_simple_action_new("open-link", NULL);
	GSimpleAction *copy_link = g_simple_action_new("copy-link", NULL);
	GSimpleAction *copy = g_simple_action_new("copy", NULL);
	GSimpleAction *paste = g_simple_action_new("paste", NULL);

	g_simple_action_set_enabled(open_link, TRUE);
	g_simple_action_set_enabled(copy_link, TRUE);
	g_simple_action_set_enabled(copy, TRUE);
	g_simple_action_set_enabled(paste, TRUE);

	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(open_link));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(copy_link));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(copy));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(paste));

	g_object_unref(open_link);
	g_object_unref(copy_link);
	g_object_unref(copy);
	g_object_unref(paste);

	return group;
}

static void test_context_link_actions_disabled_without_valid_context(void) {
	LdsTerminalTerm term = {0};
	g_autoptr(GSimpleActionGroup) group = test_context_actions_new();

	term.context_actions = group;
	term.context_source_vte = NULL;
	term.context_x = 0.0;
	term.context_y = 0.0;

	lds_terminal_vte_sync_context_actions(&term);

	GAction *open_link = g_action_map_lookup_action(G_ACTION_MAP(group), "open-link");
	GAction *copy_link = g_action_map_lookup_action(G_ACTION_MAP(group), "copy-link");
	g_assert_nonnull(open_link);
	g_assert_nonnull(copy_link);
	g_assert_false(g_action_get_enabled(open_link));
	g_assert_false(g_action_get_enabled(copy_link));
}

static void test_context_link_actions_enabled_with_valid_target(void) {
	LdsTerminalTerm term = {0};
	g_autoptr(GSimpleActionGroup) group = test_context_actions_new();

	term.context_actions = group;
	term.context_link_target = g_strdup("https://example.com");

	lds_terminal_vte_sync_context_actions(&term);

	GAction *open_link = g_action_map_lookup_action(G_ACTION_MAP(group), "open-link");
	GAction *copy_link = g_action_map_lookup_action(G_ACTION_MAP(group), "copy-link");
	g_assert_nonnull(open_link);
	g_assert_nonnull(copy_link);
	g_assert_true(g_action_get_enabled(open_link));
	g_assert_true(g_action_get_enabled(copy_link));

	g_clear_pointer(&term.context_link_target, g_free);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/lifecycle/startup/null-initial-term-is-safe",
					test_initial_term_validation_handles_null_without_crash);
	g_test_add_func("/lifecycle/startup/valid-initial-term-is-accepted",
					test_initial_term_validation_accepts_valid_term);
	g_test_add_func("/lifecycle/context-menu/model-lifecycle-stable",
					test_context_model_lifecycle_is_stable);
	g_test_add_func("/lifecycle/context-menu/model-has-link-actions",
					test_context_model_contains_link_actions);
	g_test_add_func("/lifecycle/search/cancel-debounce-removes-source",
					test_cancel_search_debounce_removes_pending_source);
	g_test_add_func("/lifecycle/search/close-selected-tab-cancels-debounce",
					test_close_selected_tab_cancels_search_debounce);
	g_test_add_func("/lifecycle/search/schedule-debounce-deduplicates-same-request",
					test_schedule_search_debounce_deduplicates_same_request);
	g_test_add_func("/lifecycle/state/remove-term-clears-cached-selected-state",
					test_remove_term_clears_cached_selected_state);
	g_test_add_func("/lifecycle/context-menu/link-actions-disabled-without-context",
					test_context_link_actions_disabled_without_valid_context);
	g_test_add_func("/lifecycle/context-menu/link-actions-enabled-with-valid-target",
					test_context_link_actions_enabled_with_valid_target);

	return g_test_run();
}
