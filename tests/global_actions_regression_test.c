/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Global action registration regression coverage.
 */

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"

static gboolean test_gtk_available(void) {
	static gint initialized = -1;
	if (initialized < 0)
		initialized = gtk_init_check() ? 1 : 0;

	return initialized > 0;
}

static GSimpleActionGroup *test_actions_new(void) {
	GSimpleActionGroup *group = g_simple_action_group_new();
	GSimpleAction *copy = g_simple_action_new("copy", NULL);
	GSimpleAction *paste = g_simple_action_new("paste", NULL);
	GSimpleAction *clear = g_simple_action_new("clear", NULL);
	GSimpleAction *reset = g_simple_action_new("reset", NULL);
	GSimpleAction *split_vertical = g_simple_action_new("split-vertical", NULL);
	GSimpleAction *close_pane = g_simple_action_new("close-pane", NULL);
	GSimpleAction *focus_next_pane = g_simple_action_new("focus-next-pane", NULL);

	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(copy));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(paste));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(clear));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(reset));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(split_vertical));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(close_pane));
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(focus_next_pane));

	g_object_unref(copy);
	g_object_unref(paste);
	g_object_unref(clear);
	g_object_unref(reset);
	g_object_unref(split_vertical);
	g_object_unref(close_pane);
	g_object_unref(focus_next_pane);

	return group;
}

static void test_global_actions_without_active_term_are_disabled(void) {
	if (!test_gtk_available()) {
		g_test_skip("No available GTK display/backend for this integration test");
		return;
	}

	LdsTerminal terminal = {0};
	GSimpleActionGroup *group = test_actions_new();
	GtkWidget *window = gtk_window_new();

	terminal.window = window;
	terminal.window_actions = group;
	terminal.tab_view = NULL;
	terminal.terms = NULL;

	lds_terminal_menu_sync_edit_actions(&terminal);

	GAction *copy = g_action_map_lookup_action(G_ACTION_MAP(group), "copy");
	GAction *paste = g_action_map_lookup_action(G_ACTION_MAP(group), "paste");
	GAction *clear = g_action_map_lookup_action(G_ACTION_MAP(group), "clear");
	GAction *reset = g_action_map_lookup_action(G_ACTION_MAP(group), "reset");
	GAction *split_vertical = g_action_map_lookup_action(G_ACTION_MAP(group), "split-vertical");
	GAction *close_pane = g_action_map_lookup_action(G_ACTION_MAP(group), "close-pane");
	GAction *focus_next_pane = g_action_map_lookup_action(G_ACTION_MAP(group), "focus-next-pane");

	g_assert_nonnull(copy);
	g_assert_nonnull(paste);
	g_assert_nonnull(clear);
	g_assert_nonnull(reset);
	g_assert_nonnull(split_vertical);
	g_assert_nonnull(close_pane);
	g_assert_nonnull(focus_next_pane);

	g_assert_false(g_action_get_enabled(copy));
	g_assert_false(g_action_get_enabled(paste));
	g_assert_false(g_action_get_enabled(clear));
	g_assert_false(g_action_get_enabled(reset));
	g_assert_false(g_action_get_enabled(split_vertical));
	g_assert_false(g_action_get_enabled(close_pane));
	g_assert_false(g_action_get_enabled(focus_next_pane));

	g_object_unref(window);
	g_object_unref(group);
}

static void test_global_actions_repeated_sync_without_context_change_is_stable(void) {
	if (!test_gtk_available())
		return;

	LdsTerminal terminal = {0};
	GSimpleActionGroup *group = test_actions_new();
	GtkWidget *window = gtk_window_new();

	terminal.window = window;
	terminal.window_actions = group;
	terminal.tab_view = NULL;
	terminal.terms = NULL;

	lds_terminal_menu_sync_edit_actions(&terminal);
	g_assert_true(terminal.menu_edit_state_valid);
	g_assert_true(terminal.menu_edit_state_term == NULL);
	g_assert_cmpuint(terminal.menu_edit_state_bits, ==, 0u);

	/* Same context must remain stable across repeated sync calls. */
	lds_terminal_menu_sync_edit_actions(&terminal);
	g_assert_true(terminal.menu_edit_state_valid);
	g_assert_true(terminal.menu_edit_state_term == NULL);
	g_assert_cmpuint(terminal.menu_edit_state_bits, ==, 0u);

	terminal.menu_paste_cache_valid = TRUE;
	terminal.menu_paste_enabled_cache = TRUE;
	lds_terminal_menu_invalidate_clipboard_cache(&terminal);
	g_assert_false(terminal.menu_paste_cache_valid);

	lds_terminal_menu_sync_edit_actions(&terminal);
	g_assert_true(terminal.menu_edit_state_valid);
	g_assert_true(terminal.menu_edit_state_term == NULL);
	g_assert_cmpuint(terminal.menu_edit_state_bits, ==, 0u);

	GAction *copy = g_action_map_lookup_action(G_ACTION_MAP(group), "copy");
	GAction *paste = g_action_map_lookup_action(G_ACTION_MAP(group), "paste");
	GAction *clear = g_action_map_lookup_action(G_ACTION_MAP(group), "clear");
	GAction *reset = g_action_map_lookup_action(G_ACTION_MAP(group), "reset");
	GAction *split_vertical = g_action_map_lookup_action(G_ACTION_MAP(group), "split-vertical");
	GAction *close_pane = g_action_map_lookup_action(G_ACTION_MAP(group), "close-pane");
	GAction *focus_next_pane = g_action_map_lookup_action(G_ACTION_MAP(group), "focus-next-pane");

	g_assert_false(g_action_get_enabled(copy));
	g_assert_false(g_action_get_enabled(paste));
	g_assert_false(g_action_get_enabled(clear));
	g_assert_false(g_action_get_enabled(reset));
	g_assert_false(g_action_get_enabled(split_vertical));
	g_assert_false(g_action_get_enabled(close_pane));
	g_assert_false(g_action_get_enabled(focus_next_pane));

	g_object_unref(window);
	g_object_unref(group);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/menu/global-actions/without-active-term-disabled",
					test_global_actions_without_active_term_are_disabled);
	g_test_add_func("/menu/global-actions/repeated-sync-stable",
					test_global_actions_repeated_sync_without_context_change_is_stable);

	return g_test_run();
}
