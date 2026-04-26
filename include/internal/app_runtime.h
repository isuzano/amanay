/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Runtime bootstrap internals.
 */

#ifndef LDS_TERMINAL_APP_RUNTIME_H
#define LDS_TERMINAL_APP_RUNTIME_H

#include <glib.h>

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Initialize process runtime policy/environment and app mode selection.
 */
void lds_terminal_runtime_bootstrap_environment(LdsTerminalState *state);
gboolean lds_terminal_runtime_use_gapplication(void);

#endif /* LDS_TERMINAL_APP_RUNTIME_H */
