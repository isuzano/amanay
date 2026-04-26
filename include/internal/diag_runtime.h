/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Runtime diagnostics internals.
 */

#ifndef LDS_TERMINAL_DIAG_RUNTIME_H
#define LDS_TERMINAL_DIAG_RUNTIME_H

#include <glib.h>
#include <signal.h>

typedef const char *(*LdsTerminalDiagStageNameFn)(gint stage);

typedef struct {
	gint stage;
	gint watchdog_stop;
	GThread *watchdog;
	GLogFunc previous_glib_log_handler;
	/* NOTE: GLib does not expose previous handler's user_data. */
	gpointer previous_glib_log_data;
	int crash_fd;
	LdsTerminalDiagStageNameFn stage_name_fn;
	gboolean glib_capture_installed;
	gboolean crash_handlers_installed;
	struct sigaction old_sigsegv;
	struct sigaction old_sigabrt;
	struct sigaction old_sigbus;
	struct sigaction old_sigill;
	struct sigaction old_sigfpe;
} LdsTerminalDiagRuntime;

/* Module contract:
 * Runtime crash/watchdog instrumentation used during app bootstrap/teardown.
 */
void lds_terminal_diag_runtime_init(LdsTerminalDiagRuntime *runtime);
void lds_terminal_diag_runtime_set_stage(LdsTerminalDiagRuntime *runtime, gint stage);
void lds_terminal_diag_runtime_install_glib_log_capture(LdsTerminalDiagRuntime *runtime);
void lds_terminal_diag_runtime_start_watchdog(LdsTerminalDiagRuntime *runtime,
											  LdsTerminalDiagStageNameFn stage_name_fn);
void lds_terminal_diag_runtime_stop_watchdog(LdsTerminalDiagRuntime *runtime);
void lds_terminal_diag_runtime_release(LdsTerminalDiagRuntime *runtime);

#endif /* LDS_TERMINAL_DIAG_RUNTIME_H */
