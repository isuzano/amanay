/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Shell spawning and environment preparation.
 */

#include <glib.h>
#include <gio/gio.h>
#include <unistd.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"
#include "internal/vte_compat.h"
#include "internal/vte_spawn.h"

static char **lds_terminal_vte_build_spawn_env(LdsTerminal *terminal, char **base_env,
											   gboolean *owned);
static const gchar *lds_terminal_preferred_shell(void);

gboolean lds_terminal_vte_spawn_child(LdsTerminalTerm *term, GtkWidget *vte, gboolean secondary,
									  const char *cwd, char **env, char **exec_override,
									  VteTerminalSpawnAsyncCallback callback, gpointer user_data) {
	if (!term || !vte || !callback)
		return FALSE;

	LdsTerminal *terminal = term->parent;
	gboolean env_owned = FALSE;
	char **spawn_env = lds_terminal_vte_build_spawn_env(terminal, env, &env_owned);

	gboolean exec_owned = FALSE;
	char **exec = NULL;
	if (exec_override && exec_override[0]) {
		exec = g_strdupv(exec_override);
		exec_owned = TRUE;
	} else {
		const gchar *shell = lds_terminal_preferred_shell();
		gboolean login_shell = terminal && terminal->parent && terminal->parent->args.login_shell;
		exec = g_new0(gchar *, login_shell ? 3 : 2);
		exec[0] = g_strdup(shell);
		if (login_shell) {
			exec[1] = g_strdup("-l");
			exec[2] = NULL;
		} else {
			exec[1] = NULL;
		}
		exec_owned = TRUE;
	}

	if (secondary)
		term->secondary_pid = -1;
	else
		term->pid = -1;
	/* Do not create utmp/wtmp/lastlog entries for each terminal tab or split pane. */
	lds_terminal_vte_terminal_spawn_async(
		VTE_TERMINAL(vte), VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP, cwd, exec,
		spawn_env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, callback, user_data);

	if (exec_owned)
		g_strfreev(exec);
	if (env_owned)
		g_strfreev(spawn_env);
	return TRUE;
}

static char **lds_terminal_vte_build_spawn_env(LdsTerminal *terminal, char **base_env,
											   gboolean *owned) {
	if (owned)
		*owned = TRUE;

	char **env = base_env ? g_strdupv(base_env) : g_get_environ();
	gint truecolor_mode = LDS_TERMINAL_TRUECOLOR_AUTO;

	if (terminal && terminal->parent)
		truecolor_mode = terminal->parent->args.truecolor_mode;

	if (truecolor_mode == LDS_TERMINAL_TRUECOLOR_FORCE) {
		env = g_environ_setenv(env, "TERM", "xterm-256color", TRUE);
		env = g_environ_setenv(env, "COLORTERM", "truecolor", TRUE);
		return env;
	}

	if (truecolor_mode == LDS_TERMINAL_TRUECOLOR_DISABLE)
		return env;

	if (!g_environ_getenv(env, "TERM"))
		env = g_environ_setenv(env, "TERM", "xterm-256color", TRUE);

	if (!g_environ_getenv(env, "COLORTERM"))
		env = g_environ_setenv(env, "COLORTERM", "truecolor", TRUE);

	return env;
}

static const gchar *lds_terminal_preferred_shell(void) {
	static const gchar *cached_shell = NULL;
	static gboolean cached_ready = FALSE;
	const gchar *shell = NULL;

	if (cached_ready)
		return cached_shell;

	shell = g_getenv("SHELL");

	if (shell && access(shell, X_OK) == 0)
		cached_shell = shell;

	if (!cached_shell && access("/bin/bash", X_OK) == 0)
		cached_shell = "/bin/bash";

	if (!cached_shell)
		cached_shell = "/bin/sh";

	cached_ready = TRUE;
	return cached_shell;
}
