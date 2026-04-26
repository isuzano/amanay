/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal search state and match navigation.
 */

#include "internal/search_engine.h"

#include <string.h>
#include <vte/vte.h>

#include "internal/search_ui.h"
#include "internal/term_registry.h"

#define LDS_SEARCH_APPROX_BYTES_THRESHOLD (1024u * 1024u)
#define LDS_SEARCH_APPROX_MATCH_LIMIT 1000u
#define LDS_SEARCH_SNAPSHOT_MIN_INTERVAL_US (80 * 1000)
#define LDS_SEARCH_THROTTLED_RETRY_MS ((LDS_SEARCH_SNAPSHOT_MIN_INTERVAL_US + 999) / 1000)

typedef struct {
	LdsTerminal *terminal;
	GBytes *haystack_bytes;
	GRegex *regex;
	GCancellable *cancellable;
	guint max_matches;
	guint request_id;
} LdsTerminalSearchCountJob;

typedef struct {
	LdsTerminal *terminal;
	guint request_id;
	guint total_matches;
	gboolean valid_regex;
	gboolean approximate;
	gboolean cancelled;
	GThread *worker_thread;
} LdsTerminalSearchCountResult;

static gpointer lds_terminal_search_count_worker(gpointer data);
static gboolean lds_terminal_search_count_result_cb(gpointer data);

void lds_terminal_search_reset_cache(LdsTerminal *terminal) {
	if (!terminal)
		return;

	lds_terminal_search_cancel_count_job(terminal, TRUE);

	g_clear_pointer(&terminal->search_last_text, g_free);
	terminal->search_last_flags = 0;
	terminal->search_last_total_matches = 0;
	terminal->search_last_valid_regex = TRUE;
	terminal->search_last_approximate = FALSE;
	terminal->search_total_cache_valid = FALSE;

	g_clear_pointer(&terminal->search_haystack_bytes, g_bytes_unref);
	terminal->search_haystack_term = NULL;
	terminal->search_haystack_generation++;
	terminal->search_haystack_snapshot_generation = 0;
	terminal->search_snapshot_throttled = FALSE;

	g_clear_pointer(&terminal->search_count_regex, g_regex_unref);
	g_clear_pointer(&terminal->search_count_regex_text, g_free);
	terminal->search_count_regex_flags = 0;
	terminal->search_count_regex_valid = TRUE;
	terminal->search_count_running = FALSE;
	terminal->search_count_reschedule = FALSE;
	terminal->search_count_active_term = NULL;
	g_clear_pointer(&terminal->search_count_active_query, g_free);
	terminal->search_count_active_opt_flags = 0;
	terminal->search_count_active_generation = 0;
	terminal->search_pending_term = NULL;
	g_clear_pointer(&terminal->search_pending_query, g_free);
	terminal->search_pending_match_case = TRUE;
	terminal->search_pending_use_regex = FALSE;
	terminal->search_pending_whole_word = FALSE;
	terminal->search_pending_opt_flags = 0;
	terminal->search_pending_generation = 0;
}

void lds_terminal_search_cancel_count_job(LdsTerminal *terminal, gboolean wait_thread) {
	if (!terminal)
		return;

	if (terminal->search_count_cancellable)
		g_cancellable_cancel(terminal->search_count_cancellable);

	if (wait_thread && terminal->search_count_thread) {
		g_thread_join(terminal->search_count_thread);
		terminal->search_count_thread = NULL;
		terminal->search_count_running = FALSE;
	}

	/* In async cancel paths, keep the result idle alive so it can finalize state. */
	if (wait_thread) {
		gint idle_id = g_atomic_int_get(&terminal->search_count_result_idle_id);
		if (idle_id > 0) {
			g_source_remove((guint)idle_id);
			g_atomic_int_set(&terminal->search_count_result_idle_id, 0);
		}
	}

	if (wait_thread) {
		g_clear_object(&terminal->search_count_cancellable);
		terminal->search_count_reschedule = FALSE;
		terminal->search_count_active_term = NULL;
		g_clear_pointer(&terminal->search_count_active_query, g_free);
		terminal->search_count_active_opt_flags = 0;
		terminal->search_count_active_generation = 0;
		terminal->search_pending_term = NULL;
		g_clear_pointer(&terminal->search_pending_query, g_free);
		terminal->search_pending_generation = 0;
	}
}

void lds_terminal_search_invalidate_snapshot(LdsTerminal *terminal) {
	if (!terminal)
		return;

	terminal->search_total_cache_valid = FALSE;
	terminal->search_last_approximate = FALSE;
	terminal->search_haystack_generation++;
	terminal->search_haystack_snapshot_generation = 0;
	terminal->search_haystack_term = NULL;
	terminal->search_snapshot_throttled = FALSE;
	g_clear_pointer(&terminal->search_haystack_bytes, g_bytes_unref);
	terminal->search_count_request_serial++;
	lds_terminal_search_cancel_count_job(terminal, FALSE);
}

GBytes *lds_terminal_search_get_or_build_snapshot(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term || !term->vte)
		return NULL;

	if (terminal->search_haystack_bytes && terminal->search_haystack_term == term &&
		terminal->search_haystack_snapshot_generation == terminal->search_haystack_generation) {
		terminal->search_snapshot_throttled = FALSE;
		return g_bytes_ref(terminal->search_haystack_bytes);
	}

	gint64 now_us = g_get_monotonic_time();
	if (terminal->search_snapshot_last_attempt_us > 0 &&
		(now_us - terminal->search_snapshot_last_attempt_us) <
			LDS_SEARCH_SNAPSHOT_MIN_INTERVAL_US) {
		terminal->search_snapshot_throttled = TRUE;
		return NULL;
	}
	terminal->search_snapshot_last_attempt_us = now_us;

	g_autofree gchar *snapshot =
		vte_terminal_get_text_format(VTE_TERMINAL(term->vte), VTE_FORMAT_TEXT);
	if (!snapshot)
		return NULL;

	/* Contract: search haystack is cached as a NUL-terminated C string. */
	gsize len = strlen(snapshot);
	GBytes *bytes = g_bytes_new_take((guint8 *)g_steal_pointer(&snapshot), len + 1u);

	g_clear_pointer(&terminal->search_haystack_bytes, g_bytes_unref);
	terminal->search_haystack_bytes = g_bytes_ref(bytes);
	terminal->search_haystack_term = term;
	terminal->search_haystack_snapshot_generation = terminal->search_haystack_generation;
	terminal->search_snapshot_throttled = FALSE;

	return bytes;
}

GRegex *lds_terminal_search_get_cached_regex(LdsTerminal *terminal, const char *query,
											 gboolean match_case, gboolean use_regex,
											 gboolean whole_word, guint opt_flags,
											 gboolean *valid_regex) {
	if (valid_regex)
		*valid_regex = TRUE;

	if (!terminal || !query || *query == '\0')
		return NULL;

	if (terminal->search_count_regex_text &&
		g_strcmp0(terminal->search_count_regex_text, query) == 0 &&
		terminal->search_count_regex_flags == opt_flags) {
		if (valid_regex)
			*valid_regex = terminal->search_count_regex_valid;
		return terminal->search_count_regex ? g_regex_ref(terminal->search_count_regex) : NULL;
	}

	g_clear_pointer(&terminal->search_count_regex, g_regex_unref);
	g_clear_pointer(&terminal->search_count_regex_text, g_free);
	terminal->search_count_regex_flags = opt_flags;

	GRegex *compiled = NULL;
	gboolean compiled_valid = TRUE;
	(void)lds_terminal_search_build_regex(query, match_case, use_regex, whole_word, &compiled,
										  &compiled_valid);

	terminal->search_count_regex_text = g_strdup(query);
	terminal->search_count_regex_valid = compiled && compiled_valid;
	if (compiled)
		terminal->search_count_regex = g_regex_ref(compiled);

	if (valid_regex)
		*valid_regex = terminal->search_count_regex_valid;
	return compiled;
}

void lds_terminal_search_schedule_count(LdsTerminal *terminal, LdsTerminalTerm *term,
										 const char *query, gboolean match_case,
										 gboolean use_regex, gboolean whole_word,
										 guint opt_flags) {
	if (!terminal || !term || !query || *query == '\0')
		return;
	if (terminal->destroyed || term->closing)
		return;
	if (!lds_terminal_has_term(terminal, term))
		return;
	if (lds_terminal_search_count_request_is_duplicate(terminal, term, query, opt_flags)) {
		lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE, TRUE);
		return;
	}

	gboolean valid_regex = TRUE;
	g_autoptr(GRegex) regex = lds_terminal_search_get_cached_regex(
		terminal, query, match_case, use_regex, whole_word, opt_flags, &valid_regex);
	if (!valid_regex || !regex)
		return;

	g_autoptr(GBytes) snapshot = lds_terminal_search_get_or_build_snapshot(terminal, term);
	if (!snapshot) {
		if (terminal->search_snapshot_throttled) {
			lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE,
												   TRUE);
			lds_terminal_search_schedule_debounce(terminal, lds_terminal_search_debounce_cb);
			if (terminal->search_debounce_id &&
				terminal->search_debounce_callback == lds_terminal_search_debounce_cb &&
				terminal->search_debounce_delay_ms < LDS_SEARCH_THROTTLED_RETRY_MS) {
				g_source_remove(terminal->search_debounce_id);
				terminal->search_debounce_id = g_timeout_add(
					LDS_SEARCH_THROTTLED_RETRY_MS, lds_terminal_search_debounce_cb, terminal);
				terminal->search_debounce_callback = lds_terminal_search_debounce_cb;
				terminal->search_debounce_delay_ms = LDS_SEARCH_THROTTLED_RETRY_MS;
			}
			return;
		}

		terminal->search_last_total_matches = 0;
		terminal->search_last_approximate = FALSE;
		terminal->search_total_cache_valid = TRUE;
		lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE, FALSE);
		return;
	}

	gsize bytes_len = 0;
	(void)g_bytes_get_data(snapshot, &bytes_len);
	guint max_matches = 0;
	if (bytes_len >= LDS_SEARCH_APPROX_BYTES_THRESHOLD)
		max_matches = LDS_SEARCH_APPROX_MATCH_LIMIT;

	if (terminal->search_count_running && terminal->search_count_thread) {
		terminal->search_count_request_serial++;
		terminal->search_count_reschedule = TRUE;
		terminal->search_pending_term = term;
		g_free(terminal->search_pending_query);
		terminal->search_pending_query = g_strdup(query);
		terminal->search_pending_match_case = match_case;
		terminal->search_pending_use_regex = use_regex;
		terminal->search_pending_whole_word = whole_word;
		terminal->search_pending_opt_flags = opt_flags;
		terminal->search_pending_generation = terminal->search_haystack_generation;
		if (terminal->search_count_cancellable)
			g_cancellable_cancel(terminal->search_count_cancellable);
		terminal->search_total_cache_valid = FALSE;
		terminal->search_last_approximate = FALSE;
		lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE, TRUE);
		return;
	}

	lds_terminal_search_cancel_count_job(terminal, TRUE);
	terminal->search_count_request_serial++;
	terminal->search_total_cache_valid = FALSE;
	terminal->search_last_approximate = FALSE;
	lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE, TRUE);
	terminal->search_count_cancellable = g_cancellable_new();
	LdsTerminalSearchCountJob *job = g_new0(LdsTerminalSearchCountJob, 1);
	job->terminal = terminal;
	job->haystack_bytes = g_bytes_ref(snapshot);
	job->regex = g_regex_ref(regex);
	job->cancellable = g_object_ref(terminal->search_count_cancellable);
	job->max_matches = max_matches;
	job->request_id = terminal->search_count_request_serial;
	terminal->search_count_active_term = term;
	g_free(terminal->search_count_active_query);
	terminal->search_count_active_query = g_strdup(query);
	terminal->search_count_active_opt_flags = opt_flags;
	terminal->search_count_active_generation = terminal->search_haystack_generation;

	terminal->search_count_thread =
		g_thread_new("lds-search-count", lds_terminal_search_count_worker, job);
	terminal->search_count_running = TRUE;
}

static gpointer lds_terminal_search_count_worker(gpointer data) {
	LdsTerminalSearchCountJob *job = data;
	if (!job)
		return NULL;

	gsize haystack_len = 0;
	const gchar *haystack = g_bytes_get_data(job->haystack_bytes, &haystack_len);
	(void)haystack_len;

	gboolean truncated = FALSE;
	guint total = lds_terminal_search_count_matches_with_regex(
		haystack, job->regex, job->max_matches, &truncated, job->cancellable);

	LdsTerminalSearchCountResult *result = g_new0(LdsTerminalSearchCountResult, 1);
	result->terminal = job->terminal;
	result->request_id = job->request_id;
	result->total_matches = total;
	result->valid_regex = TRUE;
	result->approximate = truncated;
	result->cancelled = g_cancellable_is_cancelled(job->cancellable);
	result->worker_thread = g_thread_self();
	guint idle_id = g_idle_add(lds_terminal_search_count_result_cb, result);
	g_atomic_int_set(&job->terminal->search_count_result_idle_id, (gint)idle_id);

	g_bytes_unref(job->haystack_bytes);
	g_regex_unref(job->regex);
	g_object_unref(job->cancellable);
	g_free(job);
	return NULL;
}

static gboolean lds_terminal_search_count_result_cb(gpointer data) {
	LdsTerminalSearchCountResult *result = data;
	if (!result)
		return G_SOURCE_REMOVE;

	LdsTerminal *terminal = result->terminal;
	if (!terminal || terminal->destroyed) {
		g_free(result);
		return G_SOURCE_REMOVE;
	}

	g_atomic_int_set(&terminal->search_count_result_idle_id, 0);

	if (!terminal->search_count_thread || terminal->search_count_thread != result->worker_thread) {
		g_free(result);
		return G_SOURCE_REMOVE;
	}

	if (terminal->search_count_thread) {
		g_thread_unref(terminal->search_count_thread);
		terminal->search_count_thread = NULL;
	}
	terminal->search_count_running = FALSE;
	terminal->search_count_active_term = NULL;
	g_clear_pointer(&terminal->search_count_active_query, g_free);
	terminal->search_count_active_opt_flags = 0;
	terminal->search_count_active_generation = 0;
	g_clear_object(&terminal->search_count_cancellable);

	if (result->cancelled || result->request_id != terminal->search_count_request_serial) {
		if (terminal->search_count_reschedule && terminal->search_pending_query) {
			LdsTerminalTerm *reschedule_term = terminal->search_pending_term;
			if (!lds_terminal_has_term(terminal, reschedule_term))
				reschedule_term = lds_terminal_get_current_term(terminal);

			g_autofree gchar *pending_query = g_steal_pointer(&terminal->search_pending_query);
			gboolean pending_match_case = terminal->search_pending_match_case;
			gboolean pending_use_regex = terminal->search_pending_use_regex;
			gboolean pending_whole_word = terminal->search_pending_whole_word;
			guint pending_opt_flags = terminal->search_pending_opt_flags;
			terminal->search_pending_term = NULL;
			terminal->search_count_reschedule = FALSE;
			terminal->search_pending_generation = 0;

			if (pending_query && *pending_query && reschedule_term) {
				lds_terminal_search_schedule_count(terminal, reschedule_term, pending_query,
												   pending_match_case, pending_use_regex,
												   pending_whole_word, pending_opt_flags);
			}
		}

		g_free(result);
		return G_SOURCE_REMOVE;
	}

	terminal->search_last_total_matches = result->total_matches;
	terminal->search_last_valid_regex = result->valid_regex;
	terminal->search_last_approximate = result->approximate;
	terminal->search_total_cache_valid = TRUE;

	lds_terminal_search_update_count_label(terminal->search_count_label, result->valid_regex,
										   result->total_matches, result->approximate, FALSE);

	if (terminal->search_count_reschedule && terminal->search_pending_query) {
		LdsTerminalTerm *reschedule_term = terminal->search_pending_term;
		if (!lds_terminal_has_term(terminal, reschedule_term))
			reschedule_term = lds_terminal_get_current_term(terminal);

		g_autofree gchar *pending_query = g_steal_pointer(&terminal->search_pending_query);
		gboolean pending_match_case = terminal->search_pending_match_case;
		gboolean pending_use_regex = terminal->search_pending_use_regex;
		gboolean pending_whole_word = terminal->search_pending_whole_word;
		guint pending_opt_flags = terminal->search_pending_opt_flags;
		terminal->search_pending_term = NULL;
		terminal->search_count_reschedule = FALSE;
		terminal->search_pending_generation = 0;

		if (pending_query && *pending_query && reschedule_term) {
			lds_terminal_search_schedule_count(terminal, reschedule_term, pending_query,
											   pending_match_case, pending_use_regex,
											   pending_whole_word, pending_opt_flags);
		}
	}

	g_free(result);
	return G_SOURCE_REMOVE;
}
