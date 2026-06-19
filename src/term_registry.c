/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal ownership registry and lookup.
 */

#include "internal/term_registry.h"

#include "menu.h"
#include "internal/overview_controller.h"

static void lds_terminal_terms_assert_valid(LdsTerminal *terminal) {
#ifndef G_DISABLE_ASSERT
	if (!terminal || !terminal->terms)
		return;

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		g_assert_nonnull(term);
		g_assert_true(term->parent == terminal);
		if (terminal->tab_view && !term->closing)
			g_assert_nonnull(term->page);
		g_assert_cmpint(term->index, ==, (gint)i);
	}
#else
	(void)terminal;
#endif
}

static void lds_terminal_reindex_terms(LdsTerminal *terminal) {
	if (!terminal || !terminal->terms)
		return;

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		if (term)
			term->index = (gint)i;
	}

	lds_terminal_terms_assert_valid(terminal);
}

gboolean lds_terminal_has_term(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term || !terminal->terms)
		return FALSE;

	for (guint i = 0; i < terminal->terms->len; i++) {
		if (g_ptr_array_index(terminal->terms, i) == term)
			return TRUE;
	}

	return FALSE;
}

void lds_terminal_drop_term_from_owner(LdsTerminal *owner, LdsTerminalTerm *term) {
	if (!owner || !owner->terms || !term)
		return;

	if (owner->current_selected_term == term)
		owner->current_selected_term = NULL;
	if (owner->menu_edit_state_term == term) {
		owner->menu_edit_state_term = NULL;
		owner->menu_edit_state_bits = 0;
		owner->menu_edit_state_valid = FALSE;
	}
	if (owner->menu_paste_cache_term == term) {
		owner->menu_paste_cache_term = NULL;
		owner->menu_paste_cache_valid = FALSE;
	}
	if (owner->search_state && owner->search_state->search_haystack_term == term)
		owner->search_state->search_haystack_term = NULL;

	for (guint i = 0; i < owner->terms->len; i++) {
		if (g_ptr_array_index(owner->terms, i) == term) {
			(void)g_ptr_array_steal_index(owner->terms, i);
			break;
		}
	}
	lds_terminal_reindex_terms(owner);
	lds_terminal_menu_sync_edit_actions(owner);
	lds_terminal_update_overview_label(owner);
	lds_terminal_on_selected_term_transition(owner);
	lds_terminal_schedule_focus_current_term(owner);
	lds_terminal_terms_assert_valid(owner);
}

void lds_terminal_attach_term_to_owner(LdsTerminal *owner, LdsTerminalTerm *term,
									   AdwTabPage *page, gint position) {
	if (!owner || !owner->terms || !term || !page)
		return;

	term->parent = owner;
	term->page = page;
	term->index = -1;
	if (position >= 0 && (guint)position < owner->terms->len)
		g_ptr_array_insert(owner->terms, (guint)position, term);
	else
		g_ptr_array_add(owner->terms, term);

	lds_terminal_reindex_terms(owner);
	lds_terminal_menu_sync_edit_actions(owner);
	lds_terminal_update_overview_label(owner);
	lds_terminal_on_selected_term_transition(owner);
	lds_terminal_terms_assert_valid(owner);
}

void lds_terminal_remove_term(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term)
		return;

	gint index = -1;
	for (guint i = 0; i < terminal->terms->len; i++) {
		if (g_ptr_array_index(terminal->terms, i) == term) {
			index = (gint)i;
			break;
		}
	}

	if (index < 0)
		return;

	if (terminal->current_selected_term == term)
		terminal->current_selected_term = NULL;
	if (terminal->menu_edit_state_term == term) {
		terminal->menu_edit_state_term = NULL;
		terminal->menu_edit_state_bits = 0;
		terminal->menu_edit_state_valid = FALSE;
	}
	if (terminal->menu_paste_cache_term == term) {
		terminal->menu_paste_cache_term = NULL;
		terminal->menu_paste_cache_valid = FALSE;
	}
	if (terminal->search_state && terminal->search_state->search_haystack_term == term)
		terminal->search_state->search_haystack_term = NULL;

	g_ptr_array_remove_index(terminal->terms, index);

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *t = g_ptr_array_index(terminal->terms, i);
		t->index = i;
	}
	lds_terminal_terms_assert_valid(terminal);

	lds_terminal_menu_sync_edit_actions(terminal);

	if (terminal->terms->len == 0 && terminal->window)
		gtk_window_destroy(GTK_WINDOW(terminal->window));
	else
		lds_terminal_schedule_focus_current_term(terminal);
}
