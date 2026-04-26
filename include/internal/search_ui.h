/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Search UI internals.
 */

#ifndef LDS_TERMINAL_SEARCH_UI_H
#define LDS_TERMINAL_SEARCH_UI_H

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Owns search popover UI, UI event handlers and VTE regex application.
 */
void lds_terminal_search_dialog_init(LdsTerminal *terminal);
void lds_terminal_search_dialog_show(LdsTerminal *terminal);
gboolean lds_terminal_search_apply(LdsTerminal *terminal, gboolean forward);
void lds_terminal_search_update_regex(LdsTerminal *terminal);
void lds_terminal_search_update_count_label(GtkWidget *label, gboolean valid_regex,
											 guint total_matches, gboolean approximate,
											 gboolean pending);
gboolean lds_terminal_search_debounce_cb(gpointer data);

#endif /* LDS_TERMINAL_SEARCH_UI_H */
