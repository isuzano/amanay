/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Tab overview UI state and input handling.
 */

/* Overview policies and helpers. */

#include <adwaita.h>
#include <gtk/gtk.h>

#include "internal/lds_terminal_internal.h"

#define LDS_OVERVIEW_SEARCH_THRESHOLD 8

gboolean lds_terminal_overview_should_consume_enter(guint keyval, gboolean search_active) {
	if (!search_active)
		return FALSE;

	return keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter;
}

void lds_terminal_overview_update_search_gating(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed || !terminal->tab_overview || !terminal->tab_view)
		return;

	/*
	 * Contract:
	 * - Search UI is hidden by default for small tab sets.
	 * - Search is enabled when explicitly forced (Ctrl+F/type-to-search),
	 *   or when tab count reaches threshold.
	 */
	gint n_pages = adw_tab_view_get_n_pages(ADW_TAB_VIEW(terminal->tab_view));
	gboolean enable_search =
		terminal->overview_search_forced || n_pages >= LDS_OVERVIEW_SEARCH_THRESHOLD;
	adw_tab_overview_set_enable_search(ADW_TAB_OVERVIEW(terminal->tab_overview), enable_search);
}

gboolean lds_terminal_overview_maybe_force_search_on_key(LdsTerminal *terminal, guint keyval,
														 GdkModifierType state) {
	if (!terminal || !terminal->tab_overview || !ADW_IS_TAB_OVERVIEW(terminal->tab_overview))
		return FALSE;

	if (!adw_tab_overview_get_open(ADW_TAB_OVERVIEW(terminal->tab_overview)))
		return FALSE;

	GtkWidget *focus = NULL;
	if (terminal->window && GTK_IS_ROOT(terminal->window))
		focus = gtk_root_get_focus(GTK_ROOT(terminal->window));

	gboolean focus_is_overview_editable =
		focus && GTK_IS_EDITABLE(focus) && gtk_widget_is_ancestor(focus, terminal->tab_overview);

	if ((state & GDK_CONTROL_MASK) && (state & (GDK_ALT_MASK | GDK_SUPER_MASK)) == 0 &&
		(keyval == GDK_KEY_F || keyval == GDK_KEY_f)) {
		/* Ctrl+F always forces search while overview is open. */
		terminal->overview_search_forced = TRUE;
		lds_terminal_overview_update_search_gating(terminal);
		return TRUE;
	}

	if (!focus_is_overview_editable) {
		gboolean has_strong_mods =
			(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0;
		gunichar ch = gdk_keyval_to_unicode(keyval);
		if (!has_strong_mods && ch != 0 && g_unichar_isprint(ch) && !g_unichar_iscntrl(ch)) {
			/* Type-to-search: printable key forces search on demand. */
			terminal->overview_search_forced = TRUE;
			lds_terminal_overview_update_search_gating(terminal);
			return TRUE;
		}
	}

	return FALSE;
}
