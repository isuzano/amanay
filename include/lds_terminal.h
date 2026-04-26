/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Public terminal application API.
 */

#ifndef LDS_TERMINAL_H
#define LDS_TERMINAL_H

#include <glib.h>
#include <sys/types.h>

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkEventController GtkEventController;
typedef struct _GPtrArray GPtrArray;
typedef struct _GSimpleActionGroup GSimpleActionGroup;
typedef struct _LdsTerminal LdsTerminal;
typedef struct _LdsTerminalState LdsTerminalState;
/* Backward-compat alias: use LdsTerminalState in new code. */
typedef LdsTerminalState LdsTerminalWindow;
typedef struct _LdsTerminalTerm LdsTerminalTerm;
typedef struct _LdsTerminalSettings LdsTerminalSettings;
typedef struct _AdwTabPage AdwTabPage;

typedef enum {
	LDS_TERMINAL_TRUECOLOR_AUTO = 0,
	LDS_TERMINAL_TRUECOLOR_FORCE = 1,
	LDS_TERMINAL_TRUECOLOR_DISABLE = 2
} LdsTerminalTrueColorMode;

typedef enum {
	LDS_TERMINAL_RENDERER_AUTO = 0,
	LDS_TERMINAL_RENDERER_CAIRO = 1,
	LDS_TERMINAL_RENDERER_NGL = 2,
	LDS_TERMINAL_RENDERER_VULKAN = 3
} LdsTerminalRendererMode;

/**
 * LdsTerminalCommandArgs:
 * @executable: (nullable): Path to the executable used to launch the process.
 * @command: (nullable) (array zero-terminated=1) (element-type utf8): Command
 *   array to execute.
 * @working_directory: (nullable): Working directory for the initial tab.
 * @title: (nullable): Initial window title.
 * @tabs: (nullable): Tabs specification string.
 * @profile: (nullable): Profile name to use.
 * @login_shell: Whether the command should run as a login shell.
 * @no_remote: Whether remote control is disabled.
 * @truecolor_mode: TrueColor environment strategy.
 * @renderer_mode: GSK renderer strategy.
 * @geometry_bitmask: Geometry flags for initial window geometry.
 * @geometry_columns: Initial columns if provided.
 * @geometry_rows: Initial rows if provided.
 * @geometry_xoff: Initial X offset if provided.
 * @geometry_yoff: Initial Y offset if provided.
 *
 * Command-line arguments parsed for the terminal.
 */
typedef struct {
	gchar *executable;

	gchar **command;
	gchar *working_directory;
	gchar *title;
	gchar *tabs;
	gchar *profile;

	gboolean login_shell;
	gboolean no_remote;
	gint truecolor_mode;
	gint renderer_mode;
	gboolean compat_local_services;

	gint geometry_bitmask;
	guint geometry_columns;
	guint geometry_rows;
	gint geometry_xoff;
	gint geometry_yoff;
} LdsTerminalCommandArgs;

/* Concrete structs are intentionally hidden from the public API.
 * Internal modules use include/internal/lds_terminal_internal.h. */

/**
 * lds_terminal_create:
 * @parent: (not nullable): Global app state.
 * @args: (not nullable): Command arguments.
 *
 * Create a terminal window.
 *
 * Returns: (transfer full): A new #LdsTerminal instance.
 */
LdsTerminal *lds_terminal_create(LdsTerminalState *parent,
								 LdsTerminalCommandArgs *args) G_GNUC_WARN_UNUSED_RESULT;

/**
 * lds_terminal_destroy:
 * @terminal: (nullable): Terminal instance.
 *
 * Destroy a terminal window.
 */
void lds_terminal_destroy(LdsTerminal *terminal);

/**
 * lds_terminal_new_tab:
 * @terminal: (not nullable): Terminal instance.
 * @label: (nullable): Optional tab label.
 *
 * Create a new tab.
 */
void lds_terminal_new_tab(LdsTerminal *terminal, const char *label);
/**
 * lds_terminal_rename_current_tab:
 * @terminal: (not nullable): Terminal instance.
 *
 * Open rename flow for the current tab.
 */
void lds_terminal_rename_current_tab(LdsTerminal *terminal);

/**
 * lds_terminal_close_current_tab:
 * @terminal: (not nullable): Terminal instance.
 *
 * Close the current tab.
 */
void lds_terminal_close_current_tab(LdsTerminal *terminal);

/**
 * lds_terminal_spawn_window:
 * @terminal: (not nullable): Terminal instance.
 *
 * Spawn a new window.
 */
void lds_terminal_spawn_window(LdsTerminal *terminal);
/**
 * lds_terminal_close_window:
 * @terminal: (not nullable): Terminal instance.
 *
 * Close the current window.
 */
void lds_terminal_close_window(LdsTerminal *terminal);

/**
 * lds_terminal_copy:
 * @terminal: (not nullable): Terminal instance.
 *
 * Copy selection to clipboard.
 */
void lds_terminal_copy(LdsTerminal *terminal);
/**
 * lds_terminal_paste:
 * @terminal: (not nullable): Terminal instance.
 *
 * Paste clipboard to terminal.
 */
void lds_terminal_paste(LdsTerminal *terminal);
/**
 * lds_terminal_clear:
 * @terminal: (not nullable): Terminal instance.
 *
 * Clear terminal visible content.
 */
void lds_terminal_clear(LdsTerminal *terminal);
/**
 * lds_terminal_reset:
 * @terminal: (not nullable): Terminal instance.
 *
 * Reset terminal state and content.
 */
void lds_terminal_reset(LdsTerminal *terminal);

/**
 * lds_terminal_zoom_in:
 * @terminal: (not nullable): Terminal instance.
 *
 * Increase terminal font size.
 */
void lds_terminal_zoom_in(LdsTerminal *terminal);
/**
 * lds_terminal_zoom_out:
 * @terminal: (not nullable): Terminal instance.
 *
 * Decrease terminal font size.
 */
void lds_terminal_zoom_out(LdsTerminal *terminal);
/**
 * lds_terminal_zoom_reset:
 * @terminal: (not nullable): Terminal instance.
 *
 * Reset terminal font size.
 */
void lds_terminal_zoom_reset(LdsTerminal *terminal);
/**
 * lds_terminal_open_search:
 * @terminal: (not nullable): Terminal instance.
 *
 * Open the search dialog.
 */
void lds_terminal_open_search(LdsTerminal *terminal);
/**
 * lds_terminal_split_vertical:
 * @terminal: (not nullable): Terminal instance.
 *
 * Split current tab vertically.
 */
void lds_terminal_split_vertical(LdsTerminal *terminal);
/**
 * lds_terminal_focus_next_pane:
 * @terminal: (not nullable): Terminal instance.
 *
 * Move focus to the next pane in the active split.
 */
void lds_terminal_focus_next_pane(LdsTerminal *terminal);
/**
 * lds_terminal_close_pane:
 * @terminal: (not nullable): Terminal instance.
 *
 * Close the active pane in the current tab.
 */
void lds_terminal_close_pane(LdsTerminal *terminal);

#endif /* LDS_TERMINAL_H */
