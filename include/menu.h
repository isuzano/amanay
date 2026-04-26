/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application menu API.
 */

#ifndef MENU_H
#define MENU_H

typedef struct _LdsTerminal LdsTerminal;
typedef struct _GMenuModel GMenuModel;

/**
 * lds_terminal_menu_initialize:
 * @terminal: (not nullable): Terminal instance.
 *
 * Initialize the menu and attach it to the window.
 *
 * This creates menu widgets and registers window actions.
 */
void lds_terminal_menu_initialize(LdsTerminal *terminal);
/**
 * lds_terminal_menu_update_accelerators:
 * @terminal: (not nullable): Terminal instance.
 *
 * Update menu mnemonic handling and runtime accelerator labels.
 */
void lds_terminal_menu_update_accelerators(LdsTerminal *terminal);
/**
 * lds_terminal_menu_build_context_model:
 *
 * Build the context menu model for terminal content actions.
 *
 * Returns: (transfer full): New #GMenuModel instance.
 */
GMenuModel *lds_terminal_menu_build_context_model(void);

#endif
