/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application-wide action registration and accelerators.
 */

#include <gtk/gtk.h>

#include "internal/app_actions.h"
#include "internal/shortcuts_registry.h"

void lds_terminal_app_configure_accelerators(GtkApplication *app) {
	lds_terminal_shortcuts_apply_to_app(app);
}
