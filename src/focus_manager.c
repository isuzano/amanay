/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Focus restoration helpers for terminal widgets.
 */

#include <adwaita.h>
#include <gtk/gtk.h>

#include "internal/lds_terminal_internal.h"
#include "internal/vte_panes.h"

static gboolean lds_terminal_focus_current_term_idle(gpointer data) {
	LdsTerminal *terminal = data;
	LdsTerminalTerm *term = NULL;
	GtkWidget *active = NULL;

	if (!terminal)
		return G_SOURCE_REMOVE;

	terminal->focus_recovery_idle_id = 0;
	if (terminal->destroyed)
		return G_SOURCE_REMOVE;

	term = lds_terminal_get_current_term(terminal);
	if (!term || term->closing)
		return G_SOURCE_REMOVE;

	active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		active = term->vte;
	if (!active || !GTK_IS_WIDGET(active))
		return G_SOURCE_REMOVE;

	gtk_widget_grab_focus(active);
	return G_SOURCE_REMOVE;
}

void lds_terminal_focus_current_term(LdsTerminal *terminal) {
	LdsTerminalTerm *term = NULL;
	GtkWidget *active = NULL;

	if (!terminal || terminal->destroyed)
		return;

	term = lds_terminal_get_current_term(terminal);
	if (!term || term->closing)
		return;

	active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		active = term->vte;
	if (!active || !GTK_IS_WIDGET(active))
		return;

	gtk_widget_grab_focus(active);
}

void lds_terminal_schedule_focus_current_term(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed)
		return;

	if (terminal->focus_recovery_idle_id)
		return;

	terminal->focus_recovery_idle_id =
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, lds_terminal_focus_current_term_idle, terminal,
						NULL);
}
