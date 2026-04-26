/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Tab overview internals.
 */

#ifndef LDS_TERMINAL_OVERVIEW_CONTROLLER_H
#define LDS_TERMINAL_OVERVIEW_CONTROLLER_H

#include <adwaita.h>

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Controls tab overview state, selected-term transitions and tab transfer between windows.
 */
void lds_terminal_open_overview(LdsTerminal *terminal, gboolean open);
void lds_terminal_on_overview_button_toggled(GtkToggleButton *button, LdsTerminal *terminal);
void lds_terminal_on_overview_open_notify(GObject *object, GParamSpec *pspec,
										  LdsTerminal *terminal);
void lds_terminal_update_overview_label(LdsTerminal *terminal);
void lds_terminal_on_tab_pages_changed(GObject *object, GParamSpec *pspec,
									   LdsTerminal *terminal);
void lds_terminal_on_tab_selected_page_changed(GObject *object, GParamSpec *pspec,
											   LdsTerminal *terminal);
void lds_terminal_on_tab_bar_pressed(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y,
									 LdsTerminal *terminal);
void lds_terminal_on_tab_page_detached(AdwTabView *view, AdwTabPage *page, gint position,
									   LdsTerminal *terminal);
void lds_terminal_on_tab_page_attached(AdwTabView *view, AdwTabPage *page, gint position,
									   LdsTerminal *terminal);
AdwTabPage *lds_terminal_on_overview_create_tab(AdwTabOverview *overview, LdsTerminal *terminal);
void lds_terminal_on_selected_term_transition(LdsTerminal *terminal);

#endif /* LDS_TERMINAL_OVERVIEW_CONTROLLER_H */
