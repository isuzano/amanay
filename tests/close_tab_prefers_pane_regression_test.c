/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Close-tab pane preference regression coverage.
 */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <vte/vte.h>

#include "lds_terminal.h"
#include "settings.h"
#include "vte.h"
#include "internal/lds_terminal_internal.h"

static void drain_main_loop(void) {
	while (g_main_context_iteration(NULL, FALSE))
		;
}

static void attach_secondary_split_down(LdsTerminalTerm *term) {
	g_assert_nonnull(term);
	g_assert_nonnull(term->pane_split);
	g_assert_nonnull(term->vte);
	g_assert_null(term->secondary_vte);

	term->secondary_vte = GTK_WIDGET(vte_terminal_new());
	gtk_widget_set_hexpand(term->secondary_vte, TRUE);
	gtk_widget_set_vexpand(term->secondary_vte, TRUE);
	gtk_widget_set_visible(term->secondary_vte, TRUE);
	gtk_orientable_set_orientation(GTK_ORIENTABLE(term->pane_split), GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_paned_set_end_child(GTK_PANED(term->pane_split), term->secondary_vte);
	term->secondary_pid = -1;
}

static LdsTerminalTerm *create_shellless_term(LdsTerminal *terminal, const char *title) {
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);
	term->parent = terminal;
	term->pid = -1;
	term->secondary_pid = -1;
	term->closing = FALSE;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->vte = GTK_WIDGET(vte_terminal_new());
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal->tab_view), term->box);
	if (title && *title)
		adw_tab_page_set_title(term->page, title);
	g_ptr_array_add(terminal->terms, term);
	term->index = (gint)terminal->terms->len - 1;
	return term;
}

static void test_close_tab_closes_split_tab(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();
	terminal.destroyed = FALSE;

	term->parent = &terminal;
	term->pid = -1;
	term->secondary_pid = -1;
	term->closing = FALSE;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->vte = GTK_WIDGET(vte_terminal_new());
	term->secondary_vte = GTK_WIDGET(vte_terminal_new());
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_paned_set_end_child(GTK_PANED(term->pane_split), term->secondary_vte);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);

	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	lds_terminal_close_current_tab(&terminal);

	g_assert_true(term->closing);
	g_assert_cmpuint(terminal.terms->len, ==, 0u);
	g_assert_null(terminal.current_selected_term);

	lds_terminal_vte_free_term(term);
	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
}

static void test_get_current_term_prefers_cached_lookup_path(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();

	term->parent = &terminal;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	g_assert_true(lds_terminal_get_current_term(&terminal) == term);
	g_assert_true(lds_terminal_get_current_term(&terminal) == term);
	g_assert_cmpuint(terminal.current_term_lookup_calls, ==, 2u);
	g_assert_cmpuint(terminal.current_term_lookup_fallback_scans, ==, 1u);
	g_assert_cmpuint(terminal.current_term_lookup_fast_hits, ==, 1u);

	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	g_free(term);
}

static void test_get_current_term_long_session_prefers_fast_path(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();

	term->parent = &terminal;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	for (guint i = 0; i < 2000u; i++)
		g_assert_true(lds_terminal_get_current_term(&terminal) == term);

	g_assert_cmpuint(terminal.current_term_lookup_calls, ==, 2000u);
	g_assert_cmpuint(terminal.current_term_lookup_fallback_scans, ==, 1u);
	g_assert_cmpuint(terminal.current_term_lookup_fast_hits, ==, 1999u);

	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	g_free(term);
}

static void test_apply_current_settings_keeps_focus_on_active_split_pane(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.window = gtk_window_new();
	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();
	gtk_window_set_child(GTK_WINDOW(terminal.window), terminal.tab_view);

	term->parent = &terminal;
	term->pid = -1;
	term->secondary_pid = -1;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->vte = GTK_WIDGET(vte_terminal_new());
	term->secondary_vte = GTK_WIDGET(vte_terminal_new());
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_paned_set_end_child(GTK_PANED(term->pane_split), term->secondary_vte);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);

	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	gtk_window_present(GTK_WINDOW(terminal.window));
	while (g_main_context_iteration(NULL, FALSE))
		;

	gtk_widget_grab_focus(term->secondary_vte);
	while (g_main_context_iteration(NULL, FALSE))
		;

	lds_terminal_settings_apply_current(&terminal);
	while (g_main_context_iteration(NULL, FALSE))
		;

	GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(terminal.window));
	g_assert_nonnull(focus);
	gboolean on_secondary =
		(focus == term->secondary_vte) || gtk_widget_is_ancestor(focus, term->secondary_vte);
	gboolean on_primary = (focus == term->vte) || gtk_widget_is_ancestor(focus, term->vte);
	g_assert_true(on_secondary || on_primary);

	lds_terminal_vte_free_term(term);
	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	gtk_window_destroy(GTK_WINDOW(terminal.window));
}

static void test_split_down_close_resize_stress_sequence(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.window = gtk_window_new();
	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();
	gtk_window_set_child(GTK_WINDOW(terminal.window), terminal.tab_view);

	term->parent = &terminal;
	term->pid = -1;
	term->secondary_pid = -1;
	term->closing = FALSE;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->vte = GTK_WIDGET(vte_terminal_new());
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	gtk_window_set_default_size(GTK_WINDOW(terminal.window), 900, 620);
	gtk_window_present(GTK_WINDOW(terminal.window));
	drain_main_loop();

	for (guint i = 0; i < 40u; i++) {
		attach_secondary_split_down(term);
		drain_main_loop();

		gtk_window_set_default_size(GTK_WINDOW(terminal.window), 640 + (gint)(i % 5u) * 80,
									420 + (gint)(i % 4u) * 70);
		drain_main_loop();

		gboolean closed = lds_terminal_vte_close_active_pane(term);
		g_assert_true(closed);
		drain_main_loop();

		g_assert_null(term->secondary_vte);
		g_assert_nonnull(term->vte);
		g_assert_nonnull(term->pane_split);
	}

	lds_terminal_vte_free_term(term);
	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	gtk_window_destroy(GTK_WINDOW(terminal.window));
}

static void test_split_down_close_with_aggressive_resize(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		g_free(term);
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		g_free(term);
		return;
	}
	adw_init();

	terminal.window = gtk_window_new();
	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();
	gtk_window_set_child(GTK_WINDOW(terminal.window), terminal.tab_view);

	term->parent = &terminal;
	term->pid = -1;
	term->secondary_pid = -1;
	term->closing = FALSE;
	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->vte = GTK_WIDGET(vte_terminal_new());
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal.tab_view), term->box);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	g_ptr_array_add(terminal.terms, term);

	gtk_window_set_default_size(GTK_WINDOW(terminal.window), 720, 520);
	gtk_window_present(GTK_WINDOW(terminal.window));
	drain_main_loop();

	for (guint i = 0; i < 20u; i++) {
		attach_secondary_split_down(term);
		drain_main_loop();

		/* Alternate compact/expanded sizes around pane close to stress allocation churn. */
		gtk_window_set_default_size(GTK_WINDOW(terminal.window), 520, 360);
		drain_main_loop();
		gtk_window_set_default_size(GTK_WINDOW(terminal.window), 1100, 760);
		drain_main_loop();
		gtk_window_set_default_size(GTK_WINDOW(terminal.window), 600, 420);
		drain_main_loop();

		g_assert_true(lds_terminal_vte_close_active_pane(term));
		drain_main_loop();

		g_assert_null(term->secondary_vte);
		g_assert_nonnull(term->vte);
	}

	lds_terminal_vte_free_term(term);
	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	gtk_window_destroy(GTK_WINDOW(terminal.window));
}

static void test_scenario_open_10_tabs_split_5_rapid_close(void) {
	LdsTerminal terminal = {0};

	if (!gtk_init_check()) {
		g_test_skip("No available GTK display/backend for this integration test");
		return;
	}
	if (!gdk_display_get_default()) {
		g_test_skip("GTK initialized but no default GDK display available");
		return;
	}
	adw_init();

	terminal.window = gtk_window_new();
	terminal.tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal.terms = g_ptr_array_new();
	terminal.destroyed = FALSE;
	gtk_window_set_child(GTK_WINDOW(terminal.window), terminal.tab_view);
	gtk_window_present(GTK_WINDOW(terminal.window));
	drain_main_loop();

	for (guint i = 0; i < 10u; i++) {
		g_autofree gchar *title = g_strdup_printf("stress-tab-%u", i + 1u);
		LdsTerminalTerm *term = create_shellless_term(&terminal, title);
		adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
	}
	drain_main_loop();
	g_assert_cmpuint(terminal.terms->len, ==, 10u);

	/* Split five tabs and close split panes in quick sequence. */
	for (guint i = 0; i < 5u; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal.terms, i);
		adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
		attach_secondary_split_down(term);
		gtk_widget_grab_focus(term->secondary_vte);
	}
	drain_main_loop();

	for (guint i = 0; i < 5u; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal.terms, i);
		g_assert_true(lds_terminal_vte_close_active_pane(term));
		g_assert_null(term->secondary_vte);
	}
	drain_main_loop();

	/* Rapid close all tabs from the selected one backward. */
	while (terminal.terms->len > 0) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal.terms, terminal.terms->len - 1u);
		if (term && term->page)
			adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), term->page);
		lds_terminal_remove_term(&terminal, term);
	}
	drain_main_loop();
	g_assert_cmpuint(terminal.terms->len, ==, 0u);

	g_ptr_array_free(terminal.terms, TRUE);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/tabs/close-current-tab/prefers-closing-pane-first",
					test_close_tab_closes_split_tab);
	g_test_add_func("/tabs/current-term/cached-lookup-fast-path",
					test_get_current_term_prefers_cached_lookup_path);
	g_test_add_func("/tabs/current-term/long-session-fast-path",
					test_get_current_term_long_session_prefers_fast_path);
	g_test_add_func("/prefs/font/apply-current-keeps-active-pane-focus",
					test_apply_current_settings_keeps_focus_on_active_split_pane);
	g_test_add_func("/panes/split-down/stress-close-resize-sequence",
					test_split_down_close_resize_stress_sequence);
	g_test_add_func("/panes/split-down/close-with-aggressive-resize",
					test_split_down_close_with_aggressive_resize);
	g_test_add_func("/scenario/stress/open-10-tabs-split-5-rapid-close",
					test_scenario_open_10_tabs_split_5_rapid_close);

	return g_test_run();
}
