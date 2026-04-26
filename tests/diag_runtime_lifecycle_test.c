/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Diagnostics runtime lifecycle coverage.
 */

#include <glib.h>

#include "internal/diag.h"
#include "internal/diag_runtime.h"

static const char *test_stage_name(gint stage) {
	(void)stage;
	return "test";
}

static void test_diag_runtime_lifecycle_when_enabled(void) {
	LdsTerminalDiagRuntime runtime = {0};

	g_setenv("LDS_TERMINAL_DIAG", "1", TRUE);
	g_setenv("LDS_TERMINAL_DIAG_FILE", "/tmp/lds-terminal-diag-runtime-test.log", TRUE);
	lds_terminal_diag_init();

	lds_terminal_diag_runtime_init(&runtime);
	g_assert_cmpint(runtime.crash_fd, ==, -1);

	lds_terminal_diag_runtime_install_glib_log_capture(&runtime);
	g_assert_true(runtime.glib_capture_installed);

	lds_terminal_diag_runtime_set_stage(&runtime, 42);
	g_assert_cmpint(g_atomic_int_get(&runtime.stage), ==, 42);

	lds_terminal_diag_runtime_start_watchdog(&runtime, test_stage_name);
	lds_terminal_diag_runtime_stop_watchdog(&runtime);
	g_assert_null(runtime.watchdog);

	lds_terminal_diag_runtime_release(&runtime);
	g_assert_false(runtime.glib_capture_installed);
	g_assert_false(runtime.crash_handlers_installed);
	g_assert_cmpint(runtime.crash_fd, ==, -1);

	lds_terminal_diag_shutdown();
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/diag/runtime/lifecycle-enabled", test_diag_runtime_lifecycle_when_enabled);

	return g_test_run();
}
