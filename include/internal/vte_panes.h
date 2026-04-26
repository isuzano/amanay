/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE pane lifecycle internals.
 */

#ifndef LDS_TERMINAL_VTE_PANES_H
#define LDS_TERMINAL_VTE_PANES_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "internal/lds_terminal_internal.h"

static inline gboolean lds_terminal_vte_term_is_owned(LdsTerminalTerm *term) {
	if (!term || !term->parent || !term->parent->terms || term->parent->destroyed)
		return FALSE;

	/* Fast path: term index is maintained by tab append/remove paths. */
	if (term->index >= 0 && (guint)term->index < term->parent->terms->len &&
		g_ptr_array_index(term->parent->terms, term->index) == term)
		return TRUE;

	/* Fallback scan keeps correctness if index ever gets temporarily stale. */
	for (guint i = 0; i < term->parent->terms->len; i++) {
		if (g_ptr_array_index(term->parent->terms, i) == term) {
			term->index = (gint)i;
			return TRUE;
		}
	}

	return FALSE;
}

static inline gboolean lds_terminal_vte_term_alive(LdsTerminalTerm *term) {
	return term && !term->closing && lds_terminal_vte_term_is_owned(term) && term->parent &&
		   !term->parent->destroyed;
}

GtkWidget *lds_terminal_vte_get_active_widget(LdsTerminalTerm *term);
gboolean lds_terminal_vte_is_secondary_widget(LdsTerminalTerm *term, GtkWidget *widget);
void lds_terminal_vte_close_secondary_widget(LdsTerminalTerm *term, GtkWidget *widget);
void lds_terminal_vte_handle_secondary_spawn_ready(LdsTerminalTerm *term, GPid pid,
												   GError *error);
void lds_terminal_vte_attach_controllers(LdsTerminalTerm *term, GtkWidget *vte,
										 gboolean secondary);
void lds_terminal_vte_remove_secondary(LdsTerminalTerm *term, gboolean destroy_widget);
gboolean lds_terminal_vte_promote_secondary(LdsTerminalTerm *term, GtkWidget *old_primary);
gboolean lds_terminal_vte_split(LdsTerminalTerm *term, GtkOrientation orientation);
gboolean lds_terminal_vte_close_active_pane(LdsTerminalTerm *term);
void lds_terminal_vte_focus_next_pane(LdsTerminalTerm *term);
gboolean lds_terminal_vte_has_split(LdsTerminalTerm *term);
void lds_terminal_vte_resync_layout(LdsTerminalTerm *term);
void lds_terminal_vte_schedule_post_split_refresh_pass(GtkWidget *vte, pid_t shell_pid, guint pass);
void lds_terminal_on_child_exited(VteTerminal *vte, gint status, LdsTerminalTerm *term);
void lds_terminal_on_secondary_child_exited(VteTerminal *vte, gint status, LdsTerminalTerm *term);
void lds_terminal_on_secondary_eof(VteTerminal *vte, LdsTerminalTerm *term);
gboolean lds_terminal_on_vte_key_pressed(GtkEventControllerKey *controller, guint keyval,
										 guint keycode, GdkModifierType state,
										 LdsTerminalTerm *term);
void lds_terminal_on_vte_selection_notify(VteTerminal *vte, GParamSpec *pspec,
										  LdsTerminalTerm *term);
void lds_terminal_on_title_notify(VteTerminal *vte, GParamSpec *pspec, LdsTerminalTerm *term);
void lds_terminal_on_cwd_changed(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_eof(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_spawn_ready(VteTerminal *vte, GPid pid, GError *error, gpointer user_data);
void lds_terminal_vte_bind_gsettings(VteTerminal *vte);

#endif /* LDS_TERMINAL_VTE_PANES_H */
