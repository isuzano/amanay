/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Diagnostics lifecycle utilities.
 */

#ifndef LDS_TERMINAL_DIAG_H
#define LDS_TERMINAL_DIAG_H

#include <glib.h>

/* Module contract:
 * Lightweight diagnostics lifecycle and scoped logging utilities.
 */
void lds_terminal_diag_init(void);
void lds_terminal_diag_shutdown(void);
gboolean lds_terminal_diag_enabled(void);
void lds_terminal_diag_log(const char *scope, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

#endif /* LDS_TERMINAL_DIAG_H */
