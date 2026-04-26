/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE link handling internals.
 */

#ifndef LDS_TERMINAL_VTE_LINKS_H
#define LDS_TERMINAL_VTE_LINKS_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "internal/lds_terminal_internal.h"

void lds_terminal_vte_links_setup_regex(VteTerminal *vte);
void lds_terminal_vte_links_cleanup_term(LdsTerminalTerm *term, gboolean ui_teardown);
void lds_terminal_vte_links_on_click_primary(GtkGestureClick *gesture, int n_press, double x,
											 double y, LdsTerminalTerm *term);
void lds_terminal_vte_links_on_click_secondary(GtkGestureClick *gesture, int n_press, double x,
											   double y, LdsTerminalTerm *term);
void lds_terminal_vte_links_on_motion(GtkEventControllerMotion *motion, double x, double y,
									  LdsTerminalTerm *term);
void lds_terminal_vte_links_on_leave(GtkEventControllerMotion *motion, LdsTerminalTerm *term);
void lds_terminal_vte_links_on_contents_changed(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_vte_links_on_text_scrolled(VteTerminal *vte, gint delta, LdsTerminalTerm *term);
void lds_terminal_vte_sync_context_actions(LdsTerminalTerm *term);

#endif /* LDS_TERMINAL_VTE_LINKS_H */
