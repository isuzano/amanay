/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Link detection spans and helpers.
 */

#ifndef LDS_TERMINAL_LINK_DETECT_H
#define LDS_TERMINAL_LINK_DETECT_H

#include <glib.h>

typedef enum {
	LDS_LINK_KIND_OSC8 = 0,
	LDS_LINK_KIND_URL = 1,
	LDS_LINK_KIND_EMAIL = 2,
	LDS_LINK_KIND_DOMAIN = 3
} LdsLinkKind;

typedef struct {
	guint start_col;
	guint end_col;
	const gchar *uri;
} LdsOsc8Span;

typedef struct {
	guint start_col;
	guint end_col;
	LdsLinkKind kind;
	gchar *target;
	guint flags;
} LdsLinkSpan;

/* Module contract:
 * Deterministic per-line link detection with OSC8-aware precedence.
 */
GPtrArray *lds_link_detect_line(const gchar *line_utf8, const LdsOsc8Span *osc8_spans,
								gsize osc8_len);
void lds_link_span_free(gpointer data);

#endif /* LDS_TERMINAL_LINK_DETECT_H */
