/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Main window API.
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <glib.h>

typedef struct _LdsTerminal LdsTerminal;

/**
 * lds_terminal_window_initialize:
 * @terminal: (not nullable): Terminal instance.
 *
 * Initialize window signals and state for the active terminal window.
 *
 * This installs window-level callbacks and updates window-owned state.
 */
void lds_terminal_window_initialize(LdsTerminal *terminal);
/**
 * lds_terminal_window_close:
 * @terminal: (not nullable): Terminal instance.
 *
 * Close a terminal window and release its visible UI.
 */
void lds_terminal_window_close(LdsTerminal *terminal);
/**
 * lds_terminal_window_confirm_close:
 * @terminal: (not nullable): Terminal instance.
 *
 * Ask for close confirmation when needed.
 *
 * Returns: %TRUE if closing is allowed.
 */
gboolean lds_terminal_window_confirm_close(LdsTerminal *terminal);
/**
 * lds_terminal_window_update_title:
 * @terminal: (not nullable): Terminal instance.
 * @title: (nullable): New title.
 *
 * Update the visible window title.
 */
void lds_terminal_window_update_title(LdsTerminal *terminal, const char *title);

#endif
