/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application action registration internals.
 */

#ifndef LDS_TERMINAL_APP_ACTIONS_H
#define LDS_TERMINAL_APP_ACTIONS_H

#include <gtk/gtk.h>

/* Module contract:
 * Applies all window-level action accelerators from current settings state.
 */
void lds_terminal_app_configure_accelerators(GtkApplication *app);

#endif /* LDS_TERMINAL_APP_ACTIONS_H */
