/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Per-line link cache internals.
 */

#ifndef LDS_TERMINAL_LINK_CACHE_H
#define LDS_TERMINAL_LINK_CACHE_H

#include <glib.h>

#include "internal/link_detect.h"

typedef struct _LdsLinkLineCache LdsLinkLineCache;

/* Module contract:
 * Per-line link detection cache with targeted invalidation for visible ranges.
 */
LdsLinkLineCache *lds_link_line_cache_new(guint max_entries);
void lds_link_line_cache_free(LdsLinkLineCache *cache);
void lds_link_line_cache_invalidate_all(LdsLinkLineCache *cache);
void lds_link_line_cache_invalidate_range(LdsLinkLineCache *cache, gint row_start, gint row_end);
guint lds_link_line_cache_size(const LdsLinkLineCache *cache);

const GPtrArray *lds_link_line_cache_get_or_detect(LdsLinkLineCache *cache, gint row,
												   guint64 generation, const gchar *line_utf8,
												   const LdsOsc8Span *osc8_spans, gsize osc8_len,
												   gboolean *recomputed);

#endif /* LDS_TERMINAL_LINK_CACHE_H */
