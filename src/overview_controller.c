/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Tab overview controller and navigation behavior.
 */

#include "internal/overview_controller.h"

#include "internal/diag.h"
#include "internal/search_engine.h"
#include "internal/vte_panes.h"
#include "internal/term_registry.h"

#include "menu.h"

typedef struct {
	GtkWidget *window;
	GtkWidget *vte;
} LdsTerminalAttachedFocusData;

#define LDS_TERMINAL_TRANSFER_TERM_DATA_KEY "lds-terminal-transfer-term"

static AdwTabPage *lds_terminal_tab_page_at_coords(LdsTerminal *terminal, gdouble x, gdouble y);
static gboolean lds_terminal_on_tab_attached_focus_idle(gpointer data);

static AdwTabPage *lds_terminal_tab_page_at_coords(LdsTerminal *terminal, gdouble x, gdouble y) {
	if (!terminal || !terminal->tab_bar)
		return NULL;

	GtkWidget *picked = gtk_widget_pick(terminal->tab_bar, x, y, GTK_PICK_DEFAULT);
	for (GtkWidget *w = picked; w != NULL; w = gtk_widget_get_parent(w)) {
		GParamSpec *page_pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(w), "page");
		if (!page_pspec)
			page_pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(w), "tab-page");
		if (!page_pspec || !g_type_is_a(page_pspec->value_type, ADW_TYPE_TAB_PAGE))
			continue;

		AdwTabPage *page = NULL;
		g_object_get(w, page_pspec->name, &page, NULL);
		return page;
	}

	return NULL;
}

void lds_terminal_open_overview(LdsTerminal *terminal, gboolean open) {
	if (!terminal || terminal->destroyed || !terminal->tab_overview)
		return;

	if (!open)
		terminal->overview_search_forced = FALSE;
	lds_terminal_overview_update_search_gating(terminal);
	adw_tab_overview_set_open(ADW_TAB_OVERVIEW(terminal->tab_overview), open);
	if (!open)
		lds_terminal_schedule_focus_current_term(terminal);
	if (open && terminal->tab_view)
		adw_tab_view_invalidate_thumbnails(ADW_TAB_VIEW(terminal->tab_view));
}

void lds_terminal_on_overview_button_toggled(GtkToggleButton *button, LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed)
		return;

	gboolean active = gtk_toggle_button_get_active(button);
	lds_terminal_open_overview(terminal, active);
}

void lds_terminal_on_overview_open_notify(GObject *object, GParamSpec *pspec,
										  LdsTerminal *terminal) {
	(void)pspec;
	if (!terminal || terminal->destroyed || !terminal->overview_button)
		return;

	gboolean open = adw_tab_overview_get_open(ADW_TAB_OVERVIEW(object));
	if (open && terminal->tab_view)
		adw_tab_view_invalidate_thumbnails(ADW_TAB_VIEW(terminal->tab_view));
	if (!open)
		terminal->overview_search_forced = FALSE;
	lds_terminal_overview_update_search_gating(terminal);
	if (!open)
		lds_terminal_schedule_focus_current_term(terminal);

	g_signal_handlers_block_by_func(terminal->overview_button,
									G_CALLBACK(lds_terminal_on_overview_button_toggled), terminal);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(terminal->overview_button), open);
	g_signal_handlers_unblock_by_func(
		terminal->overview_button, G_CALLBACK(lds_terminal_on_overview_button_toggled), terminal);
}

void lds_terminal_update_overview_label(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed || !terminal->tab_view || !terminal->overview_label ||
		!GTK_IS_WIDGET(terminal->overview_label))
		return;

	gint count = adw_tab_view_get_n_pages(ADW_TAB_VIEW(terminal->tab_view));
	if (count <= 1) {
		gtk_widget_set_visible(terminal->overview_label, FALSE);
		return;
	}

	g_autofree gchar *text = g_strdup_printf("%d Tabs", count);
	gtk_label_set_text(GTK_LABEL(terminal->overview_label), text);
	gtk_widget_set_visible(terminal->overview_label, TRUE);
}

void lds_terminal_on_tab_pages_changed(GObject *object, GParamSpec *pspec,
									   LdsTerminal *terminal) {
	(void)object;
	(void)pspec;
	if (!terminal || terminal->destroyed)
		return;

	lds_terminal_update_overview_label(terminal);
	lds_terminal_overview_update_search_gating(terminal);
	lds_terminal_on_selected_term_transition(terminal);
}

void lds_terminal_on_tab_selected_page_changed(GObject *object, GParamSpec *pspec,
											   LdsTerminal *terminal) {
	(void)object;
	(void)pspec;
	if (!terminal || terminal->destroyed)
		return;

	lds_terminal_on_selected_term_transition(terminal);
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (term && term->page)
		lds_terminal_window_update_title(terminal, adw_tab_page_get_title(term->page));
	lds_terminal_focus_current_term(terminal);
	lds_terminal_schedule_focus_current_term(terminal);
}

void lds_terminal_on_tab_bar_pressed(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y,
									 LdsTerminal *terminal) {
	(void)gesture;
	if (!terminal || terminal->destroyed || n_press < 1 || !terminal->tab_bar ||
		!terminal->tab_view)
		return;

	AdwTabPage *page = lds_terminal_tab_page_at_coords(terminal, x, y);
	if (!page)
		return;

	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal->tab_view), page);
	g_object_unref(page);
}

void lds_terminal_on_tab_page_detached(AdwTabView *view, AdwTabPage *page, gint position,
									   LdsTerminal *terminal) {
	(void)view;
	(void)position;

	if (!terminal || terminal->destroyed || !page || !terminal->terms)
		return;

	LdsTerminalTerm *term = lds_terminal_find_term_by_page(terminal, page, FALSE);
	if (!term || term->closing) {
		g_object_set_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY, NULL);
		return;
	}

	if (term->parent != terminal || term->page != page || !lds_terminal_has_term(terminal, term)) {
		lds_terminal_diag_log("tabs", "detach ignored inconsistent term=%p page=%p", (void *)term,
							  (void *)page);
		g_object_set_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY, NULL);
		return;
	}

	lds_terminal_drop_term_from_owner(terminal, term);
	if (lds_terminal_has_term(terminal, term)) {
		lds_terminal_diag_log("tabs", "detach failed to drop term=%p page=%p", (void *)term,
							  (void *)page);
		g_object_set_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY, NULL);
		return;
	}

	term->page = NULL;
	g_object_set_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY, term);
}

void lds_terminal_on_tab_page_attached(AdwTabView *view, AdwTabPage *page, gint position,
									   LdsTerminal *terminal) {
	(void)view;

	if (!terminal || terminal->destroyed || !page || !terminal->terms)
		return;

	LdsTerminalTerm *term = g_object_get_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY);
	if (!term)
		return;

	g_object_set_data(G_OBJECT(page), LDS_TERMINAL_TRANSFER_TERM_DATA_KEY, NULL);

	if (term->closing) {
		lds_terminal_diag_log("tabs", "attach ignored closing term=%p page=%p", (void *)term,
							  (void *)page);
		return;
	}

	if (lds_terminal_has_term(terminal, term)) {
		lds_terminal_diag_log("tabs", "attach skipped already-owned term=%p page=%p", (void *)term,
							  (void *)page);
		adw_tab_view_set_selected_page(ADW_TAB_VIEW(view), page);
		return;
	}

	lds_terminal_attach_term_to_owner(terminal, term, page, position);
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(view), page);

	LdsTerminalAttachedFocusData *focus_data = g_new0(LdsTerminalAttachedFocusData, 1);
	if (terminal->window && GTK_IS_WINDOW(terminal->window))
		focus_data->window = g_object_ref(terminal->window);
	if (term->vte && GTK_IS_WIDGET(term->vte))
		focus_data->vte = g_object_ref(term->vte);
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, lds_terminal_on_tab_attached_focus_idle, focus_data,
					NULL);
}

static gboolean lds_terminal_on_tab_attached_focus_idle(gpointer data) {
	LdsTerminalAttachedFocusData *focus_data = data;
	if (!focus_data)
		return G_SOURCE_REMOVE;

	if (focus_data->window && GTK_IS_WINDOW(focus_data->window))
		gtk_window_present(GTK_WINDOW(focus_data->window));
	if (focus_data->vte && GTK_IS_WIDGET(focus_data->vte))
		gtk_widget_grab_focus(focus_data->vte);

	if (focus_data->window)
		g_object_unref(focus_data->window);
	if (focus_data->vte)
		g_object_unref(focus_data->vte);
	g_free(focus_data);
	return G_SOURCE_REMOVE;
}

void lds_terminal_on_selected_term_transition(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed)
		return;

	LdsTerminalTerm *previous = terminal->current_selected_term;
	LdsTerminalTerm *current = lds_terminal_get_current_term(terminal);
	if (previous == current)
		return;

	terminal->current_selected_term = current;
	lds_terminal_search_reset_cache(terminal);
	lds_terminal_menu_sync_edit_actions(terminal);
}

AdwTabPage *lds_terminal_on_overview_create_tab(AdwTabOverview *overview, LdsTerminal *terminal) {
	(void)overview;
	if (!terminal)
		return NULL;

	LdsTerminalTerm *term = lds_terminal_tabs_create(terminal, NULL, NULL, NULL, NULL);
	if (!term)
		return NULL;

	lds_terminal_tabs_append(terminal, term);
	return term->page;
}
