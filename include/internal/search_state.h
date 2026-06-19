/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Internal search state storage.
 */

#ifndef LDS_TERMINAL_SEARCH_STATE_H
#define LDS_TERMINAL_SEARCH_STATE_H

#include <gio/gio.h>
#include <glib.h>

typedef struct _LdsTerminalSearchState {
	guint search_debounce_id;
	GSourceFunc search_debounce_callback;
	guint search_debounce_delay_ms;
	gchar *search_last_text;
	guint search_last_flags;
	guint search_last_total_matches;
	gboolean search_last_valid_regex;
	gboolean search_last_approximate;
	gboolean search_total_cache_valid;
	GBytes *search_haystack_bytes;
	struct _LdsTerminalTerm *search_haystack_term;
	guint search_haystack_generation;
	guint search_haystack_snapshot_generation;
	gint64 search_snapshot_last_attempt_us;
	gboolean search_snapshot_throttled;
	GRegex *search_count_regex;
	gchar *search_count_regex_text;
	guint search_count_regex_flags;
	gboolean search_count_regex_valid;
	guint search_count_request_serial;
	GThread *search_count_thread;
	GCancellable *search_count_cancellable;
	gint search_count_result_idle_id;
	gboolean search_count_running;
	gboolean search_count_reschedule;
	struct _LdsTerminalTerm *search_count_active_term;
	gchar *search_count_active_query;
	guint search_count_active_opt_flags;
	guint search_count_active_generation;
	struct _LdsTerminalTerm *search_pending_term;
	gchar *search_pending_query;
	gboolean search_pending_match_case;
	gboolean search_pending_use_regex;
	gboolean search_pending_whole_word;
	guint search_pending_opt_flags;
	guint search_pending_generation;
} LdsTerminalSearchState;

static inline LdsTerminalSearchState *lds_terminal_search_state_new(void) {
	return g_new0(LdsTerminalSearchState, 1);
}

static inline void lds_terminal_search_state_free(LdsTerminalSearchState *state) {
	if (!state)
		return;

	if (state->search_debounce_id)
		g_source_remove(state->search_debounce_id);
	g_clear_pointer(&state->search_last_text, g_free);
	g_clear_pointer(&state->search_haystack_bytes, g_bytes_unref);
	g_clear_pointer(&state->search_count_regex, g_regex_unref);
	g_clear_pointer(&state->search_count_regex_text, g_free);
	g_clear_pointer(&state->search_count_active_query, g_free);
	g_clear_pointer(&state->search_pending_query, g_free);
	g_clear_object(&state->search_count_cancellable);
	g_free(state);
}

#endif /* LDS_TERMINAL_SEARCH_STATE_H */
