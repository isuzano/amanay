/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Search policy and debounce helpers.
 */

/* Search policies and shared search helpers. */

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib.h>

#include "internal/lds_terminal_internal.h"
#include "settings.h"

enum {
	LDS_SEARCH_FLAG_MATCH_CASE = 1u << 0,
	LDS_SEARCH_FLAG_USE_REGEX = 1u << 1,
	LDS_SEARCH_FLAG_WHOLE_WORD = 1u << 2,
	LDS_SEARCH_FLAG_WRAP = 1u << 3
};

static gchar *lds_terminal_search_build_pattern(const char *query, gboolean use_regex,
												gboolean whole_word) {
	if (!query || *query == '\0')
		return NULL;

	g_autofree gchar *base = use_regex ? g_strdup(query) : g_regex_escape_string(query, -1);
	if (!base)
		return NULL;

	if (!whole_word)
		return g_steal_pointer(&base);

	return g_strdup_printf("\\b%s\\b", base);
}

guint lds_terminal_search_compute_debounce_ms(gboolean regex_enabled, guint scrollback_lines) {
	if (scrollback_lines >= 30000)
		return regex_enabled ? 700u : 300u;

	if (scrollback_lines >= 10000)
		return regex_enabled ? 500u : 220u;

	return regex_enabled ? 350u : 150u;
}

gboolean lds_terminal_search_build_regex(const char *query, gboolean match_case, gboolean use_regex,
										 gboolean whole_word, GRegex **regex_out,
										 gboolean *valid_regex) {
	if (regex_out)
		*regex_out = NULL;
	if (valid_regex)
		*valid_regex = TRUE;

	g_autofree gchar *pattern = lds_terminal_search_build_pattern(query, use_regex, whole_word);
	if (!pattern)
		return FALSE;

	GRegexCompileFlags compile_flags = G_REGEX_MULTILINE;
	if (!match_case)
		compile_flags |= G_REGEX_CASELESS;

	g_autoptr(GError) error = NULL;
	GRegex *regex = g_regex_new(pattern, compile_flags, 0, &error);
	if (!regex) {
		if (valid_regex)
			*valid_regex = FALSE;
		return FALSE;
	}

	if (regex_out)
		*regex_out = regex;
	else
		g_regex_unref(regex);

	return TRUE;
}

guint lds_terminal_search_count_matches_with_regex(const char *haystack, GRegex *regex,
												   guint max_matches, gboolean *truncated,
												   GCancellable *cancellable) {
	if (truncated)
		*truncated = FALSE;

	if (!haystack || !regex)
		return 0;

	g_autoptr(GError) error = NULL;
	g_autoptr(GMatchInfo) info = NULL;
	g_regex_match(regex, haystack, 0, &info);

	guint count = 0;
	while (info && g_match_info_matches(info)) {
		count++;
		if (max_matches > 0 && count >= max_matches) {
			if (truncated)
				*truncated = TRUE;
			break;
		}

		if (cancellable && (count % 32u) == 0u && g_cancellable_is_cancelled(cancellable))
			break;

		if (!g_match_info_next(info, &error) || error)
			break;
	}

	return count;
}

guint lds_terminal_search_count_matches_for_query(const char *haystack, const char *query,
												  gboolean match_case, gboolean use_regex,
												  gboolean whole_word, gboolean *valid_regex) {
	if (valid_regex)
		*valid_regex = TRUE;

	if (!haystack || !query || *query == '\0')
		return 0;

	g_autoptr(GRegex) regex = NULL;
	if (!lds_terminal_search_build_regex(query, match_case, use_regex, whole_word, &regex,
										 valid_regex)) {
		return 0;
	}

	return lds_terminal_search_count_matches_with_regex(haystack, regex, 0, NULL, NULL);
}

void lds_terminal_search_read_options(LdsTerminal *terminal, gboolean *match_case,
									  gboolean *use_regex, gboolean *whole_word, gboolean *wrap,
									  guint *opt_flags) {
	gboolean local_match_case = TRUE;
	gboolean local_use_regex = FALSE;
	gboolean local_whole_word = FALSE;
	gboolean local_wrap = TRUE;
	guint local_flags = 0;

	if (terminal && terminal->search_match_case)
		local_match_case =
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal->search_match_case));

	if (terminal && terminal->search_regex)
		local_use_regex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal->search_regex));

	if (terminal && terminal->search_whole_word)
		local_whole_word =
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal->search_whole_word));

	if (terminal && terminal->search_wrap)
		local_wrap = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal->search_wrap));

	if (local_match_case)
		local_flags |= LDS_SEARCH_FLAG_MATCH_CASE;
	if (local_use_regex)
		local_flags |= LDS_SEARCH_FLAG_USE_REGEX;
	if (local_whole_word)
		local_flags |= LDS_SEARCH_FLAG_WHOLE_WORD;
	if (local_wrap)
		local_flags |= LDS_SEARCH_FLAG_WRAP;

	if (match_case)
		*match_case = local_match_case;
	if (use_regex)
		*use_regex = local_use_regex;
	if (whole_word)
		*whole_word = local_whole_word;
	if (wrap)
		*wrap = local_wrap;
	if (opt_flags)
		*opt_flags = local_flags;
}

void lds_terminal_search_schedule_debounce(LdsTerminal *terminal, GSourceFunc callback) {
	if (!terminal || !callback)
		return;

	LdsTerminalSearchState *search_state = lds_terminal_search_state_ensure(terminal);
	if (!search_state)
		return;

	gboolean regex_enabled = FALSE;
	if (terminal->search_regex)
		regex_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal->search_regex));

	guint debounce_ms =
		lds_terminal_search_compute_debounce_ms(regex_enabled, lds_terminal_settings_scrollback());
	debounce_ms = (guint)MAX(debounce_ms, 1u);

	if (search_state->search_debounce_id && search_state->search_debounce_callback == callback &&
		search_state->search_debounce_delay_ms == debounce_ms) {
		return;
	}

	if (search_state->search_debounce_id) {
		g_source_remove(search_state->search_debounce_id);
		search_state->search_debounce_id = 0;
	}

	search_state->search_debounce_id = g_timeout_add(debounce_ms, callback, terminal);
	search_state->search_debounce_callback = callback;
	search_state->search_debounce_delay_ms = debounce_ms;
}

gboolean lds_terminal_validate_initial_term(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (term)
		return TRUE;

	g_warning("Failed to create initial terminal tab");
	if (terminal && terminal->window)
		gtk_window_destroy(GTK_WINDOW(terminal->window));

	return FALSE;
}

void lds_terminal_cancel_search_debounce(LdsTerminal *terminal) {
	LdsTerminalSearchState *search_state = terminal ? terminal->search_state : NULL;
	if (!search_state || search_state->search_debounce_id == 0)
		return;

	g_source_remove(search_state->search_debounce_id);
	search_state->search_debounce_id = 0;
	search_state->search_debounce_callback = NULL;
	search_state->search_debounce_delay_ms = 0;
}
