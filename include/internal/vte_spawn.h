/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE spawn helpers.
 */

#ifndef LDS_TERMINAL_VTE_SPAWN_H
#define LDS_TERMINAL_VTE_SPAWN_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Spawns primary/secondary shell processes for terminal panes.
 */
gboolean lds_terminal_vte_spawn_child(LdsTerminalTerm *term, GtkWidget *vte, gboolean secondary,
									  const char *cwd, char **env, char **exec_override,
									  VteTerminalSpawnAsyncCallback callback, gpointer user_data);

#endif /* LDS_TERMINAL_VTE_SPAWN_H */
