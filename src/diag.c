/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Diagnostic logging helpers and policy checks.
 */

#include "internal/diag.h"

#include <glib.h>
#include <stdio.h>
#include <stdarg.h>

static gboolean diag_enabled = FALSE;
static gint64 diag_t0_us = 0;
static FILE *diag_log_stream = NULL;
static gboolean diag_log_stream_owned = FALSE;

void lds_terminal_diag_init(void) {
	diag_enabled = g_strcmp0(g_getenv("LDS_TERMINAL_DIAG"), "1") == 0;
	diag_t0_us = g_get_monotonic_time();

	if (!diag_enabled || diag_log_stream)
		return;

	const char *path = g_getenv("LDS_TERMINAL_DIAG_FILE");
	if (!path || !*path) {
		diag_log_stream = stderr;
		diag_log_stream_owned = FALSE;
		return;
	}

	FILE *f = fopen(path, "a");
	if (f) {
		diag_log_stream = f;
		diag_log_stream_owned = TRUE;
	} else {
		diag_log_stream = stderr;
		diag_log_stream_owned = FALSE;
	}
}

void lds_terminal_diag_shutdown(void) {
	if (!diag_log_stream)
		return;

	if (diag_log_stream_owned && diag_log_stream != stderr)
		fclose(diag_log_stream);

	diag_log_stream = NULL;
	diag_log_stream_owned = FALSE;
}

gboolean lds_terminal_diag_enabled(void) {
	return diag_enabled;
}

void lds_terminal_diag_log(const char *scope, const char *fmt, ...) {
	if (!diag_enabled)
		return;

	g_autofree gchar *rendered = NULL;
	va_list ap;
	va_start(ap, fmt);
	rendered = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	const gint64 now_us = g_get_monotonic_time();
	const gdouble ms = (now_us - diag_t0_us) / 1000.0;
	FILE *out = diag_log_stream ? diag_log_stream : stderr;

	fprintf(out, "[%9.3f ms] [%s] %s\n", ms, scope ? scope : "diag", rendered ? rendered : "");
	fflush(out);
}
