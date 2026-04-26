/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application runtime bootstrap and mode selection.
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>

#include "internal/app_args.h"
#include "internal/app_runtime.h"
#include "internal/diag.h"

static void lds_terminal_maybe_set_schema_dir_from_build_tree(void);

void lds_terminal_runtime_bootstrap_environment(LdsTerminalState *state) {
	if (!state)
		return;

	lds_terminal_maybe_set_schema_dir_from_build_tree();

	gboolean compat_local_services =
		state->args.compat_local_services ||
		g_strcmp0(g_getenv("LDS_TERMINAL_COMPAT_LOCAL_SERVICES"), "1") == 0;

	if (compat_local_services) {
		g_setenv("GIO_USE_VFS", "local", TRUE);
		g_setenv("ADW_DISABLE_PORTAL", "1", TRUE);
		g_setenv("GTK_USE_PORTAL", "0", TRUE);
		gtk_disable_portals();
		lds_terminal_diag_log("runtime", "compat local services enabled");
	}

	const char *renderer_value = lds_terminal_renderer_env_value(state->args.renderer_mode);

	if (renderer_value)
		g_setenv("GSK_RENDERER", renderer_value, TRUE);
	else
		g_unsetenv("GSK_RENDERER");

	lds_terminal_diag_log("runtime", "renderer mode=%s env=%s",
						  renderer_value ? renderer_value : "auto",
						  g_getenv("GSK_RENDERER") ? g_getenv("GSK_RENDERER") : "(unset)");
}

gboolean lds_terminal_runtime_use_gapplication(void) {
	/*
	 * Default is GtkApplication mode. Compatibility local mode is opt-in.
	 * Legacy LDS_TERMINAL_USE_GAPPLICATION is still honored when set.
	 */
	const char *compat_local = g_getenv("LDS_TERMINAL_COMPAT_LOCAL");
	if (g_strcmp0(compat_local, "1") == 0)
		return FALSE;

	const char *legacy = g_getenv("LDS_TERMINAL_USE_GAPPLICATION");
	if (legacy && *legacy)
		return g_strcmp0(legacy, "1") == 0;

	return TRUE;
}

static void lds_terminal_maybe_set_schema_dir_from_build_tree(void) {
	const char *schema_dir_env = g_getenv("GSETTINGS_SCHEMA_DIR");
	if (schema_dir_env && *schema_dir_env)
		return;

	g_autoptr(GError) error = NULL;
	g_autofree gchar *exe_path = NULL;
#ifdef __linux__
	exe_path = g_file_read_link("/proc/self/exe", &error);
#elif defined(__FreeBSD__)
	/* FreeBSD may not have procfs mounted; in that case discovery is skipped. */
	exe_path = g_file_read_link("/proc/curproc/file", &error);
#endif
	if (!exe_path || !*exe_path)
		return;

	g_autofree gchar *exe_dir = g_path_get_dirname(exe_path);
	if (!exe_dir || !*exe_dir)
		return;

	/* Assumes Meson build layout: build/src/lds-terminal -> build/data/gschemas.compiled. */
	g_autofree gchar *candidate_schema_dir = g_build_filename(exe_dir, "..", "data", NULL);
	g_autofree gchar *compiled_schema =
		g_build_filename(candidate_schema_dir, "gschemas.compiled", NULL);

	if (g_file_test(compiled_schema, G_FILE_TEST_IS_REGULAR)) {
		g_setenv("GSETTINGS_SCHEMA_DIR", candidate_schema_dir, TRUE);
		lds_terminal_diag_log("runtime", "using local schema dir: %s", candidate_schema_dir);
	}
}
