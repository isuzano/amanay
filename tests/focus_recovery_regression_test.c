/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Focus recovery regression coverage.
 */

#include <adwaita.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "internal/lds_terminal_internal.h"
#include "internal/overview_controller.h"

static void drain_main_loop(void) {
	while (g_main_context_iteration(NULL, FALSE))
		;
}

static LdsTerminalTerm *create_focus_term(LdsTerminal *terminal, const char *title) {
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
	gtk_widget_set_visible(term->pane_split, TRUE);
	gtk_widget_set_visible(term->vte, TRUE);
	gtk_widget_set_visible(term->box, TRUE);
	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal->tab_view), term->box);
	if (title && *title)
		adw_tab_page_set_title(term->page, title);
	g_ptr_array_add(terminal->terms, term);
	term->index = (gint)terminal->terms->len - 1;
	return term;
}

static void test_selected_tab_recovers_vte_focus(void) {
	LdsTerminal terminal = {0};
	LdsTerminalTerm *first = NULL;
	LdsTerminalTerm *second = NULL;

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
	gtk_window_set_child(GTK_WINDOW(terminal.window), terminal.tab_view);

	first = create_focus_term(&terminal, "first");
	second = create_focus_term(&terminal, "second");

	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), first->page);
	gtk_window_present(GTK_WINDOW(terminal.window));
	drain_main_loop();

	gtk_widget_grab_focus(first->vte);
	drain_main_loop();

	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal.tab_view), second->page);
	lds_terminal_on_tab_selected_page_changed(G_OBJECT(terminal.tab_view), NULL, &terminal);
	drain_main_loop();

	GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(terminal.window));
	g_assert_nonnull(focus);
	gboolean on_second = (focus == second->vte) || gtk_widget_is_ancestor(focus, second->vte);
	g_assert_true(on_second);

	lds_terminal_vte_free_term(first);
	lds_terminal_vte_free_term(second);
	g_ptr_array_set_size(terminal.terms, 0);
	g_ptr_array_free(terminal.terms, TRUE);
	gtk_window_destroy(GTK_WINDOW(terminal.window));
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/focus/recovery/selected-tab-restores-vte-focus",
					test_selected_tab_recovers_vte_focus);
	return g_test_run();
}
