/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal registry internals.
 */

#ifndef LDS_TERMINAL_TERM_REGISTRY_H
#define LDS_TERMINAL_TERM_REGISTRY_H

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Owns term membership, reindexing, transfer between windows and final removal.
 */
gboolean lds_terminal_has_term(LdsTerminal *terminal, LdsTerminalTerm *term);
void lds_terminal_drop_term_from_owner(LdsTerminal *owner, LdsTerminalTerm *term);
void lds_terminal_attach_term_to_owner(LdsTerminal *owner, LdsTerminalTerm *term,
									   AdwTabPage *page, gint position);
void lds_terminal_remove_term(LdsTerminal *terminal, LdsTerminalTerm *term);

#endif /* LDS_TERMINAL_TERM_REGISTRY_H */
