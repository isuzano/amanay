/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Search engine internals.
 */

#ifndef LDS_TERMINAL_SEARCH_ENGINE_H
#define LDS_TERMINAL_SEARCH_ENGINE_H

#include "internal/lds_terminal_internal.h"

/* Module contract:
 * Owns search snapshot/cache/regex state and count-job cancellation primitives.
 */
void lds_terminal_search_reset_cache(LdsTerminal *terminal);
void lds_terminal_search_cancel_count_job(LdsTerminal *terminal, gboolean wait_thread);
void lds_terminal_search_invalidate_snapshot(LdsTerminal *terminal);
GBytes *lds_terminal_search_get_or_build_snapshot(LdsTerminal *terminal, LdsTerminalTerm *term);
GRegex *lds_terminal_search_get_cached_regex(LdsTerminal *terminal, const char *query,
											 gboolean match_case, gboolean use_regex,
											 gboolean whole_word, guint opt_flags,
											 gboolean *valid_regex);
void lds_terminal_search_schedule_count(LdsTerminal *terminal, LdsTerminalTerm *term,
										 const char *query, gboolean match_case,
										 gboolean use_regex, gboolean whole_word,
										 guint opt_flags);

#endif /* LDS_TERMINAL_SEARCH_ENGINE_H */
