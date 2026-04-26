/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Runtime diagnostics and lifecycle tracing.
 */

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "internal/diag.h"
#include "internal/diag_runtime.h"

static void lds_terminal_diag_crash_handler(int sig, siginfo_t *info, void *ucontext);
static void lds_terminal_glib_log_capture(const gchar *log_domain, GLogLevelFlags log_level,
										  const gchar *message, gpointer user_data);
static gpointer lds_terminal_diag_watchdog_thread(gpointer data);
static void lds_terminal_diag_install_crash_handlers(LdsTerminalDiagRuntime *runtime);
static void lds_terminal_diag_restore_crash_handlers(LdsTerminalDiagRuntime *runtime);
static gsize lds_terminal_append_uint(char *buf, gsize cap, gsize pos, unsigned int value);

static LdsTerminalDiagRuntime *diag_signal_runtime = NULL;

void lds_terminal_diag_runtime_init(LdsTerminalDiagRuntime *runtime) {
	if (!runtime)
		return;

	*runtime = (LdsTerminalDiagRuntime){0};
	runtime->crash_fd = -1;
}

void lds_terminal_diag_runtime_set_stage(LdsTerminalDiagRuntime *runtime, gint stage) {
	if (!runtime)
		return;

	g_atomic_int_set(&runtime->stage, stage);
}

void lds_terminal_diag_runtime_install_glib_log_capture(LdsTerminalDiagRuntime *runtime) {
	if (!runtime || !lds_terminal_diag_enabled())
		return;
	if (runtime->glib_capture_installed)
		return;

	lds_terminal_diag_install_crash_handlers(runtime);
	runtime->previous_glib_log_handler =
		g_log_set_default_handler(lds_terminal_glib_log_capture, runtime);
	runtime->previous_glib_log_data = NULL;
	runtime->glib_capture_installed = TRUE;
}

void lds_terminal_diag_runtime_start_watchdog(LdsTerminalDiagRuntime *runtime,
											  LdsTerminalDiagStageNameFn stage_name_fn) {
	if (!runtime || !lds_terminal_diag_enabled())
		return;

	runtime->stage_name_fn = stage_name_fn;
	g_atomic_int_set(&runtime->watchdog_stop, 0);
	runtime->watchdog =
		g_thread_new("lds-diag-watchdog", lds_terminal_diag_watchdog_thread, runtime);
}

void lds_terminal_diag_runtime_stop_watchdog(LdsTerminalDiagRuntime *runtime) {
	if (!runtime)
		return;

	g_atomic_int_set(&runtime->watchdog_stop, 1);

	if (runtime->watchdog) {
		g_thread_join(runtime->watchdog);
		runtime->watchdog = NULL;
	}
}

void lds_terminal_diag_runtime_release(LdsTerminalDiagRuntime *runtime) {
	if (!runtime)
		return;

	lds_terminal_diag_runtime_stop_watchdog(runtime);
	lds_terminal_diag_restore_crash_handlers(runtime);

	if (runtime->glib_capture_installed && runtime->previous_glib_log_handler) {
		g_log_set_default_handler(runtime->previous_glib_log_handler,
								  runtime->previous_glib_log_data);
	}
	runtime->glib_capture_installed = FALSE;
	runtime->previous_glib_log_handler = NULL;
	runtime->previous_glib_log_data = NULL;
	runtime->stage_name_fn = NULL;

	if (runtime->crash_fd > STDERR_FILENO)
		close(runtime->crash_fd);

	runtime->crash_fd = -1;
	if (diag_signal_runtime == runtime)
		diag_signal_runtime = NULL;
}

static gpointer lds_terminal_diag_watchdog_thread(gpointer data) {
	LdsTerminalDiagRuntime *runtime = data;
	if (!runtime)
		return NULL;

	while (!g_atomic_int_get(&runtime->watchdog_stop)) {
		g_usleep(500000);
		const char *stage_text = "unknown";
		if (runtime->stage_name_fn)
			stage_text = runtime->stage_name_fn(g_atomic_int_get(&runtime->stage));
		lds_terminal_diag_log("watchdog", "stage=%s", stage_text ? stage_text : "unknown");
	}

	return NULL;
}

static void lds_terminal_glib_log_capture(const gchar *log_domain, GLogLevelFlags log_level,
										  const gchar *message, gpointer user_data) {
	LdsTerminalDiagRuntime *runtime = user_data;
	const char *domain = log_domain ? log_domain : "GLib";
	const char *level = "LOG";

	if ((log_level & G_LOG_LEVEL_ERROR) != 0)
		level = "ERROR";
	else if ((log_level & G_LOG_LEVEL_CRITICAL) != 0)
		level = "CRITICAL";
	else if ((log_level & G_LOG_LEVEL_WARNING) != 0)
		level = "WARNING";
	else if ((log_level & G_LOG_LEVEL_MESSAGE) != 0)
		level = "MESSAGE";
	else if ((log_level & G_LOG_LEVEL_INFO) != 0)
		level = "INFO";
	else if ((log_level & G_LOG_LEVEL_DEBUG) != 0)
		level = "DEBUG";

	lds_terminal_diag_log("glib", "%s %s: %s", domain, level, message ? message : "(null)");

	if (runtime && runtime->previous_glib_log_handler) {
		runtime->previous_glib_log_handler(log_domain, log_level, message,
										   runtime->previous_glib_log_data);
	}
}

static void lds_terminal_diag_install_crash_handlers(LdsTerminalDiagRuntime *runtime) {
	if (!runtime)
		return;
	if (runtime->crash_handlers_installed)
		return;

	const char *path = g_getenv("LDS_TERMINAL_DIAG_FILE");
	if (!path || !*path)
		path = "/tmp/lds-terminal.log";

	runtime->crash_fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0600);
	if (runtime->crash_fd < 0)
		runtime->crash_fd = STDERR_FILENO;
	diag_signal_runtime = runtime;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = lds_terminal_diag_crash_handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGSEGV, &sa, &runtime->old_sigsegv);
	sigaction(SIGABRT, &sa, &runtime->old_sigabrt);
	sigaction(SIGBUS, &sa, &runtime->old_sigbus);
	sigaction(SIGILL, &sa, &runtime->old_sigill);
	sigaction(SIGFPE, &sa, &runtime->old_sigfpe);
	runtime->crash_handlers_installed = TRUE;
}

static void lds_terminal_diag_restore_crash_handlers(LdsTerminalDiagRuntime *runtime) {
	if (!runtime || !runtime->crash_handlers_installed)
		return;

	sigaction(SIGSEGV, &runtime->old_sigsegv, NULL);
	sigaction(SIGABRT, &runtime->old_sigabrt, NULL);
	sigaction(SIGBUS, &runtime->old_sigbus, NULL);
	sigaction(SIGILL, &runtime->old_sigill, NULL);
	sigaction(SIGFPE, &runtime->old_sigfpe, NULL);
	runtime->crash_handlers_installed = FALSE;
}

static void lds_terminal_diag_crash_handler(int sig, siginfo_t *info, void *ucontext) {
	(void)ucontext;
	(void)info;
	int fd = STDERR_FILENO;
	if (diag_signal_runtime && diag_signal_runtime->crash_fd >= 0)
		fd = diag_signal_runtime->crash_fd;

	char header[128];
	gsize pos = 0;
	const char prefix[] = "\n[crash] signal=";
	const char suffix[] = " pid=";
	const char nl = '\n';

	if (sizeof(prefix) - 1 < sizeof(header)) {
		memcpy(header + pos, prefix, sizeof(prefix) - 1);
		pos += sizeof(prefix) - 1;
	}
	pos = lds_terminal_append_uint(header, sizeof(header), pos, (unsigned int)sig);
	if (pos + (sizeof(suffix) - 1) < sizeof(header)) {
		memcpy(header + pos, suffix, sizeof(suffix) - 1);
		pos += sizeof(suffix) - 1;
	}
	pos = lds_terminal_append_uint(header, sizeof(header), pos, (unsigned int)getpid());
	if (pos < sizeof(header))
		header[pos++] = nl;

	if (pos > 0)
		write(fd, header, pos);

	void *frames[64];
	int count = backtrace(frames, 64);
	backtrace_symbols_fd(frames, count, fd);
	write(fd, "\n", 1);

	_exit(128 + sig);
}

static gsize lds_terminal_append_uint(char *buf, gsize cap, gsize pos, unsigned int value) {
	char tmp[16];
	gsize n = 0;

	do {
		tmp[n++] = (char)('0' + (value % 10u));
		value /= 10u;
	} while (value > 0u && n < sizeof(tmp));

	while (n > 0 && pos < cap)
		buf[pos++] = tmp[--n];

	return pos;
}
