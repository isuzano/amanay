/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Application runtime policy checks.
 */

#include <glib.h>

#include "lds_terminal.h"
#include "internal/app_runtime.h"
#include "internal/app_args.h"
#include "internal/lds_terminal_internal.h"

static void test_runtime_use_gapplication_flag(void) {
	g_unsetenv("LDS_TERMINAL_COMPAT_LOCAL");
	g_unsetenv("LDS_TERMINAL_USE_GAPPLICATION");
	g_assert_true(lds_terminal_runtime_use_gapplication());

	g_setenv("LDS_TERMINAL_COMPAT_LOCAL", "1", TRUE);
	g_assert_false(lds_terminal_runtime_use_gapplication());

	g_unsetenv("LDS_TERMINAL_COMPAT_LOCAL");
	g_setenv("LDS_TERMINAL_USE_GAPPLICATION", "1", TRUE);
	g_assert_true(lds_terminal_runtime_use_gapplication());

	g_setenv("LDS_TERMINAL_USE_GAPPLICATION", "0", TRUE);
	g_assert_false(lds_terminal_runtime_use_gapplication());
}

static void test_runtime_bootstrap_sets_renderer_env(void) {
	LdsTerminalState state = {0};

	g_setenv("GSETTINGS_SCHEMA_DIR", "/tmp", TRUE);
	g_setenv("GSK_RENDERER", "legacy", TRUE);

	state.args.renderer_mode = LDS_TERMINAL_RENDERER_CAIRO;
	state.args.compat_local_services = FALSE;
	lds_terminal_runtime_bootstrap_environment(&state);
	g_assert_cmpstr(g_getenv("GSK_RENDERER"), ==, "cairo");

	state.args.renderer_mode = LDS_TERMINAL_RENDERER_AUTO;
	lds_terminal_runtime_bootstrap_environment(&state);
	g_assert_null(g_getenv("GSK_RENDERER"));
}

static void test_runtime_bootstrap_compat_local_services(void) {
	LdsTerminalState state = {0};
	state.args.renderer_mode = LDS_TERMINAL_RENDERER_AUTO;
	state.args.compat_local_services = TRUE;

	g_setenv("GSETTINGS_SCHEMA_DIR", "/tmp", TRUE);
	g_unsetenv("GIO_USE_VFS");
	g_unsetenv("ADW_DISABLE_PORTAL");
	g_unsetenv("GTK_USE_PORTAL");

	lds_terminal_runtime_bootstrap_environment(&state);

	g_assert_cmpstr(g_getenv("GIO_USE_VFS"), ==, "local");
	g_assert_cmpstr(g_getenv("ADW_DISABLE_PORTAL"), ==, "1");
	g_assert_cmpstr(g_getenv("GTK_USE_PORTAL"), ==, "0");
}

static void test_renderer_env_mapping(void) {
	g_assert_null(lds_terminal_renderer_env_value(LDS_TERMINAL_RENDERER_AUTO));
	g_assert_cmpstr(lds_terminal_renderer_env_value(LDS_TERMINAL_RENDERER_CAIRO), ==, "cairo");
	g_assert_cmpstr(lds_terminal_renderer_env_value(LDS_TERMINAL_RENDERER_NGL), ==, "ngl");
	g_assert_cmpstr(lds_terminal_renderer_env_value(LDS_TERMINAL_RENDERER_VULKAN), ==, "vulkan");
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/runtime/use-gapplication-flag", test_runtime_use_gapplication_flag);
	g_test_add_func("/runtime/bootstrap/renderer-env", test_runtime_bootstrap_sets_renderer_env);
	g_test_add_func("/runtime/bootstrap/compat-local-services",
					test_runtime_bootstrap_compat_local_services);
	g_test_add_func("/runtime/renderer-env-mapping", test_renderer_env_mapping);

	return g_test_run();
}
