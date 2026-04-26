/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Shortcut registry internals.
 */

#ifndef LDS_TERMINAL_SHORTCUTS_REGISTRY_H
#define LDS_TERMINAL_SHORTCUTS_REGISTRY_H

#include <glib.h>
#include <gtk/gtk.h>

typedef enum {
	LDS_TERMINAL_SHORTCUT_GROUP_WINDOW = 0,
	LDS_TERMINAL_SHORTCUT_GROUP_EDITING = 1,
	LDS_TERMINAL_SHORTCUT_GROUP_VIEW = 2,
	LDS_TERMINAL_SHORTCUT_GROUP_PANE = 3
} LdsTerminalShortcutGroup;

typedef const char *(*LdsTerminalShortcutGetter)(void);
typedef void (*LdsTerminalShortcutSetter)(const char *accel);

typedef struct {
	const char *title;
	const char *action_name;
	LdsTerminalShortcutGroup group;
	LdsTerminalShortcutGetter getter;
	LdsTerminalShortcutSetter setter;
} LdsTerminalShortcutBinding;

/* Module contract:
 * Single source of truth for shortcut metadata, normalization and conflicts.
 */
guint lds_terminal_shortcuts_count(void);
const LdsTerminalShortcutBinding *lds_terminal_shortcuts_at(guint index);
const LdsTerminalShortcutBinding *lds_terminal_shortcuts_find_by_action(const char *action_name);
const char *lds_terminal_shortcuts_accel_or_null(const char *accel);
gchar *lds_terminal_shortcuts_normalize_accel(const char *accel);
gboolean lds_terminal_shortcuts_has_conflict(const LdsTerminalShortcutBinding *self,
											 const char *accel,
											 const LdsTerminalShortcutBinding **hit);
void lds_terminal_shortcuts_apply_to_app(GtkApplication *app);

#endif /* LDS_TERMINAL_SHORTCUTS_REGISTRY_H */
