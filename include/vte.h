/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Public terminal widget API.
 */

#ifndef VTE_H
#define VTE_H

#include <gtk/gtk.h>

typedef struct _LdsTerminal LdsTerminal;
typedef struct _LdsTerminalTerm LdsTerminalTerm;

/**
 * lds_terminal_vte_create_term:
 * @terminal: (not nullable): Terminal instance.
 * @label: (nullable): Optional tab label.
 * @cwd: (nullable): Working directory.
 * @env: (nullable) (array zero-terminated=1) (element-type utf8): Environment.
 * @exec: (nullable) (array zero-terminated=1) (element-type utf8): Command.
 *
 * Create a VTE-backed terminal tab.
 *
 * Returns: (transfer full): A new #LdsTerminalTerm instance.
 */
LdsTerminalTerm *lds_terminal_vte_create_term(LdsTerminal *terminal, const char *label,
											  const char *cwd, char **env, char **exec);

/**
 * lds_terminal_vte_free_term:
 * @term: (nullable): Terminal term instance.
 *
 * Free a #LdsTerminalTerm instance.
 */
void lds_terminal_vte_free_term(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_apply_settings:
 * @terminal: (not nullable): Terminal instance.
 * @term: (not nullable): Terminal term instance.
 *
 * Apply settings to a VTE terminal.
 */
void lds_terminal_vte_apply_settings(LdsTerminal *terminal, LdsTerminalTerm *term);
/**
 * lds_terminal_vte_copy:
 * @term: (not nullable): Terminal term instance.
 *
 * Copy selection to clipboard.
 */
gboolean lds_terminal_vte_copy(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_paste:
 * @term: (not nullable): Terminal term instance.
 *
 * Paste clipboard content.
 */
gboolean lds_terminal_vte_paste(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_clear:
 * @term: (not nullable): Terminal term instance.
 *
 * Clear visible VTE content.
 *
 * Returns: %TRUE when the operation was accepted.
 */
gboolean lds_terminal_vte_clear(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_reset:
 * @term: (not nullable): Terminal term instance.
 *
 * Reset terminal state and clear buffers.
 *
 * Returns: %TRUE when the operation was accepted.
 */
gboolean lds_terminal_vte_reset(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_split:
 * @term: (not nullable): Terminal term instance.
 * @orientation: Split orientation.
 *
 * Split active terminal pane.
 *
 * Returns: %TRUE when split succeeded.
 */
gboolean lds_terminal_vte_split(LdsTerminalTerm *term, GtkOrientation orientation);
/**
 * lds_terminal_vte_close_active_pane:
 * @term: (not nullable): Terminal term instance.
 *
 * Close active pane in a split view.
 *
 * Returns: %TRUE when pane close was performed.
 */
gboolean lds_terminal_vte_close_active_pane(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_focus_next_pane:
 * @term: (not nullable): Terminal term instance.
 *
 * Move input focus to next pane.
 */
void lds_terminal_vte_focus_next_pane(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_active_has_selection:
 * @term: (not nullable): Terminal term instance.
 *
 * Check whether active pane has text selection.
 *
 * Returns: %TRUE when selection exists.
 */
gboolean lds_terminal_vte_active_has_selection(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_active_clipboard_has_text:
 * @term: (not nullable): Terminal term instance.
 *
 * Check whether clipboard currently contains text.
 *
 * Returns: %TRUE when paste action is available.
 */
gboolean lds_terminal_vte_active_clipboard_has_text(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_has_split:
 * @term: (not nullable): Terminal term instance.
 *
 * Check whether current term is split.
 *
 * Returns: %TRUE when term is in split mode.
 */
gboolean lds_terminal_vte_has_split(LdsTerminalTerm *term);
/**
 * lds_terminal_vte_resync_layout:
 * @term: (not nullable): Terminal term instance.
 *
 * Reapply pane sizing and style to keep split layout consistent.
 */
void lds_terminal_vte_resync_layout(LdsTerminalTerm *term);

#endif
