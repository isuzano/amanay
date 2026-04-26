/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Command-line parsing internals.
 */

#ifndef LDS_TERMINAL_APP_ARGS_H
#define LDS_TERMINAL_APP_ARGS_H

#include <glib.h>

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Parse CLI arguments into app state and expose renderer env translation.
 */
gboolean lds_terminal_parse_args(LdsTerminalState *state, int *argc, char ***argv, GError **error);
const char *lds_terminal_renderer_env_value(gint mode);

#endif /* LDS_TERMINAL_APP_ARGS_H */
