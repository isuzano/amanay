/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal tab management API.
 */

#ifndef TABS_H
#define TABS_H

#include <glib.h>

typedef struct _LdsTerminal LdsTerminal;
typedef struct _LdsTerminalTerm LdsTerminalTerm;

/**
 * lds_terminal_tabs_update_alt:
 * @terminal: (not nullable): Terminal instance.
 *
 * Update Alt+N shortcut state and related mnemonic handling.
 */
void lds_terminal_tabs_update_alt(LdsTerminal *terminal);
/**
 * lds_terminal_tabs_set_position:
 * @terminal: (not nullable): Terminal instance.
 * @position: GtkPositionType value.
 *
 * Set the tab bar position.
 */
void lds_terminal_tabs_set_position(LdsTerminal *terminal, int position);

#endif
