/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * User-facing terminal actions and command handlers.
 */

/* User-facing actions (tabs, clipboard, zoom, panes). */

#include <adwaita.h>
#include <gtk/gtk.h>

#include "settings.h"
#include "tabs.h"
#include "vte.h"
#include "window.h"
#include "internal/diag.h"
#include "internal/lds_terminal_internal.h"

#define LDS_TERMINAL_SCALE_MIN 0.3
#define LDS_TERMINAL_SCALE_MAX 5.0

static guint lds_terminal_perf_log_interval(void) {
	static gsize once = 0;
	static guint interval = 512u;

	if (g_once_init_enter(&once)) {
		const char *raw = g_getenv("LDS_TERMINAL_PERF_LOG_INTERVAL");
		guint parsed = 0u;
		if (raw && *raw) {
			guint64 v = g_ascii_strtoull(raw, NULL, 10);
			if (v > 0u && v <= G_MAXUINT)
				parsed = (guint)v;
		}
		interval = parsed > 0u ? parsed : 512u;
		g_once_init_leave(&once, 1);
	}

	return interval;
}

static gdouble lds_terminal_perf_log_min_fallback_pct(void) {
	static gsize once = 0;
	static gdouble threshold = 2.0;

	if (g_once_init_enter(&once)) {
		const char *raw = g_getenv("LDS_TERMINAL_PERF_LOG_MIN_FALLBACK_PCT");
		gdouble parsed = 0.0;
		if (raw && *raw) {
			char *end = NULL;
			parsed = g_ascii_strtod(raw, &end);
			if (!end || end == raw || parsed < 0.0 || parsed > 100.0)
				parsed = 0.0;
		}
		threshold = parsed > 0.0 ? parsed : 2.0;
		g_once_init_leave(&once, 1);
	}

	return threshold;
}

typedef enum {
	LDS_TERMINAL_CONFIRM_CLOSE_PANE,
	LDS_TERMINAL_CONFIRM_CLOSE_TAB
} LdsTerminalConfirmCloseKind;

typedef struct {
	GWeakRef window_ref;
	guint close_async_token;
	LdsTerminalConfirmCloseKind kind;
} LdsTerminalConfirmCloseData;

typedef struct {
	GWeakRef window_ref;
	AdwTabPage *page;
	GtkWidget *entry;
} LdsTerminalRenameTabData;

static void lds_terminal_confirm_close_response(AdwAlertDialog *dialog, const char *response,
												LdsTerminalConfirmCloseData *data);
static void lds_terminal_close_current_tab_now(LdsTerminal *terminal);
static void lds_terminal_close_pane_now(LdsTerminal *terminal);
static void lds_terminal_request_job_close_confirmation(LdsTerminal *terminal,
														LdsTerminalConfirmCloseKind kind,
														guint jobs_affected);
static void lds_terminal_rename_tab_dialog_response(AdwAlertDialog *dialog, const char *response,
													LdsTerminalRenameTabData *data);
static gboolean lds_terminal_request_confirm_close_current_tab_if_needed(LdsTerminal *terminal,
																		 LdsTerminalTerm *term);
static gboolean lds_terminal_request_confirm_close_active_pane_if_needed(LdsTerminal *terminal,
																		 LdsTerminalTerm *term);
static gdouble lds_terminal_zoom_snap(gdouble scale);

static void lds_terminal_confirm_close_response(AdwAlertDialog *dialog, const char *response,
												LdsTerminalConfirmCloseData *data) {
	(void)dialog;
	if (!data)
		goto out;

	GtkWidget *window = g_weak_ref_get(&data->window_ref);
	LdsTerminal *terminal = NULL;
	if (window)
		terminal = g_object_get_data(G_OBJECT(window), LDS_TERMINAL_WINDOW_DATA_KEY);

	if (!terminal || terminal->destroyed || terminal->close_async_token != data->close_async_token)
		goto out;

	if (g_strcmp0(response, "close") != 0)
		goto out;

	if (data->kind == LDS_TERMINAL_CONFIRM_CLOSE_PANE)
		lds_terminal_close_pane_now(terminal);
	else
		lds_terminal_close_current_tab_now(terminal);

out:
	if (window)
		g_object_unref(window);
	g_weak_ref_clear(&data->window_ref);
	g_free(data);
}

static void lds_terminal_request_job_close_confirmation(LdsTerminal *terminal,
														LdsTerminalConfirmCloseKind kind,
														guint jobs_affected) {
	if (!terminal || !terminal->window)
		return;

	const char *title = kind == LDS_TERMINAL_CONFIRM_CLOSE_PANE ? "Close pane?" : "Close tab?";
	const char *target = kind == LDS_TERMINAL_CONFIRM_CLOSE_PANE ? "pane" : "tab";
	g_autofree gchar *body = NULL;
	if (jobs_affected > 1) {
		body = g_strdup_printf(
			"This will terminate %u running foreground jobs in the active %s. Continue?",
			jobs_affected, target);
	} else {
		body = g_strdup_printf("This will terminate a running foreground job in the active %s. "
							   "Continue?",
							   target);
	}

	AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(title, body));
	adw_alert_dialog_add_response(dlg, "cancel", "Cancel");
	adw_alert_dialog_add_response(dlg, "close", "Close");
	adw_alert_dialog_set_response_appearance(dlg, "close", ADW_RESPONSE_DESTRUCTIVE);
	adw_alert_dialog_set_default_response(dlg, "cancel");
	adw_alert_dialog_set_close_response(dlg, "cancel");

	LdsTerminalConfirmCloseData *data = g_new0(LdsTerminalConfirmCloseData, 1);
	g_weak_ref_init(&data->window_ref, G_OBJECT(terminal->window));
	data->close_async_token = terminal->close_async_token;
	data->kind = kind;
	g_signal_connect(dlg, "response", G_CALLBACK(lds_terminal_confirm_close_response), data);
	adw_dialog_present(ADW_DIALOG(dlg), terminal->window);
}

static gboolean lds_terminal_request_confirm_close_current_tab_if_needed(LdsTerminal *terminal,
																		 LdsTerminalTerm *term) {
	if (!terminal || !term || !lds_terminal_settings_confirm_running_process_enabled())
		return FALSE;

	guint tab_jobs = lds_terminal_vte_term_running_job_count(term);
	if (tab_jobs == 0)
		return FALSE;

	lds_terminal_request_job_close_confirmation(terminal, LDS_TERMINAL_CONFIRM_CLOSE_TAB,
												 tab_jobs);
	return TRUE;
}

static gboolean lds_terminal_request_confirm_close_active_pane_if_needed(LdsTerminal *terminal,
																		 LdsTerminalTerm *term) {
	if (!terminal || !term || !lds_terminal_settings_confirm_running_process_enabled())
		return FALSE;

	guint active_jobs = lds_terminal_vte_active_has_running_job(term) ? 1u : 0u;
	if (active_jobs == 0)
		return FALSE;

	lds_terminal_request_job_close_confirmation(terminal, LDS_TERMINAL_CONFIRM_CLOSE_PANE,
												 active_jobs);
	return TRUE;
}

static gdouble lds_terminal_zoom_snap(gdouble scale) {
	gdouble snapped = (gdouble)((gint)(scale * 10.0 + 0.5)) / 10.0;
	return CLAMP(snapped, LDS_TERMINAL_SCALE_MIN, LDS_TERMINAL_SCALE_MAX);
}

LdsTerminalTerm *lds_terminal_find_term_by_page(LdsTerminal *terminal, AdwTabPage *page,
												gboolean skip_closing) {
	if (!terminal || !terminal->terms || !page)
		return NULL;

	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		if (term && term->page == page && (!skip_closing || !term->closing))
			return term;
	}

	return NULL;
}

static void lds_terminal_rename_tab_dialog_response(AdwAlertDialog *dialog, const char *response,
													LdsTerminalRenameTabData *data) {
	(void)dialog;
	GtkWidget *window = NULL;
	LdsTerminal *terminal = NULL;

	if (data)
		window = g_weak_ref_get(&data->window_ref);
	if (window)
		terminal = g_object_get_data(G_OBJECT(window), LDS_TERMINAL_WINDOW_DATA_KEY);

	if (g_strcmp0(response, "rename") == 0 && data && terminal && !terminal->destroyed) {
		LdsTerminalTerm *term = lds_terminal_find_term_by_page(terminal, data->page, TRUE);
		if (term) {
			const char *raw = gtk_editable_get_text(GTK_EDITABLE(data->entry));
			g_autofree gchar *trimmed = g_strdup(raw ? raw : "");
			g_strstrip(trimmed);

			g_clear_pointer(&term->custom_tab_title, g_free);
			if (trimmed && *trimmed)
				term->custom_tab_title = g_strdup(trimmed);

			lds_terminal_vte_refresh_tab_label(term);
			lds_terminal_toast(terminal, term->custom_tab_title ? "Tab renamed" : "Tab name reset");
		}
	}

	if (window)
		g_object_unref(window);
	if (data) {
		g_weak_ref_clear(&data->window_ref);
		g_clear_object(&data->page);
		g_free(data);
	}
}

LdsTerminalTerm *lds_terminal_get_current_term(LdsTerminal *terminal) {
	if (!terminal || !terminal->tab_view || !terminal->terms)
		return NULL;
	terminal->current_term_lookup_calls++;

	AdwTabPage *page = adw_tab_view_get_selected_page(ADW_TAB_VIEW(terminal->tab_view));
	if (!page)
		return NULL;

	LdsTerminalTerm *cached = terminal->current_selected_term;
	if (cached && cached->parent == terminal && !cached->closing && cached->page == page) {
		terminal->current_term_lookup_fast_hits++;
		return cached;
	}

	terminal->current_term_lookup_fallback_scans++;
	guint interval = lds_terminal_perf_log_interval();
	if (lds_terminal_diag_enabled() && interval > 0u &&
		(terminal->current_term_lookup_fallback_scans % interval) == 0u) {
		const gdouble hit_ratio = terminal->current_term_lookup_calls > 0
									  ? (100.0 * (gdouble)terminal->current_term_lookup_fast_hits /
										 (gdouble)terminal->current_term_lookup_calls)
									  : 0.0;
		const gdouble fallback_ratio = 100.0 - hit_ratio;
		if (fallback_ratio < lds_terminal_perf_log_min_fallback_pct())
			goto lookup_fallback_scan;
		lds_terminal_diag_log(
			"perf",
			"current-term fallback scans=%" G_GUINT64_FORMAT " calls=%" G_GUINT64_FORMAT
			" fast-hits=%" G_GUINT64_FORMAT " hit-ratio=%.1f%% fallback-ratio=%.1f%%",
			terminal->current_term_lookup_fallback_scans, terminal->current_term_lookup_calls,
			terminal->current_term_lookup_fast_hits, hit_ratio, fallback_ratio);
	}

lookup_fallback_scan:
	for (guint i = 0; i < terminal->terms->len; i++) {
		LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
		if (term->page == page) {
			terminal->current_selected_term = term;
			return term;
		}
	}

	terminal->current_selected_term = NULL;
	return NULL;
}

void lds_terminal_toast(LdsTerminal *terminal, const char *text) {
	if (!terminal || !terminal->toast_overlay || !text || !*text)
		return;

	AdwToast *toast = adw_toast_new(text);
	adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(terminal->toast_overlay), toast);
}

void lds_terminal_new_tab(LdsTerminal *terminal, const char *label) {
	if (!terminal)
		return;

	LdsTerminalTerm *term = lds_terminal_tabs_create(terminal, label, NULL, NULL, NULL);
	if (!term) {
		g_warning("lds_terminal_new_tab: failed to create term");
		return;
	}

	lds_terminal_tabs_append(terminal, term);
	lds_terminal_toast(terminal, "New tab");
}

void lds_terminal_rename_current_tab(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed || !terminal->window)
		return;

	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term || term->closing || !term->page)
		return;

	AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Rename Tab", NULL));
	adw_alert_dialog_add_response(dialog, "cancel", "Cancel");
	adw_alert_dialog_add_response(dialog, "rename", "Rename");
	adw_alert_dialog_set_default_response(dialog, "rename");
	adw_alert_dialog_set_close_response(dialog, "cancel");

	GtkWidget *entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Tab name");
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	adw_alert_dialog_set_extra_child(dialog, entry);

	const char *initial =
		term->custom_tab_title ? term->custom_tab_title : adw_tab_page_get_title(term->page);
	gtk_editable_set_text(GTK_EDITABLE(entry), initial ? initial : "");
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);

	LdsTerminalRenameTabData *data = g_new0(LdsTerminalRenameTabData, 1);
	g_weak_ref_init(&data->window_ref, G_OBJECT(terminal->window));
	data->page = term->page ? g_object_ref(term->page) : NULL;
	data->entry = entry;

	g_signal_connect(dialog, "response", G_CALLBACK(lds_terminal_rename_tab_dialog_response), data);
	adw_dialog_present(ADW_DIALOG(dialog), terminal->window);
	gtk_widget_grab_focus(entry);
}

void lds_terminal_close_current_tab(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	if (lds_terminal_request_confirm_close_current_tab_if_needed(terminal, term))
		return;

	lds_terminal_close_current_tab_now(terminal);
}

static void lds_terminal_close_current_tab_now(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	lds_terminal_tabs_close(terminal, term, LDS_TERMINAL_TABS_CLOSE_BY_USER);
}

void lds_terminal_spawn_window(LdsTerminal *terminal) {
	if (!terminal || !terminal->parent)
		return;

	LdsTerminal *created = lds_terminal_create(terminal->parent, &(LdsTerminalCommandArgs){0});
	if (!created)
		g_warning("Failed to spawn new terminal window");
}

void lds_terminal_close_window(LdsTerminal *terminal) {
	lds_terminal_window_close(terminal);
}

void lds_terminal_copy(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (term) {
		if (lds_terminal_vte_copy(term))
			lds_terminal_toast(terminal, "Copied");
		else
			lds_terminal_toast(terminal, "No selection to copy");
	}
}

void lds_terminal_clear(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (term) {
		if (lds_terminal_vte_clear(term))
			lds_terminal_toast(terminal, "Cleared");
		else
			lds_terminal_toast(terminal, "Unable to clear terminal");
	}
}

void lds_terminal_reset(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (term) {
		if (lds_terminal_vte_reset(term))
			lds_terminal_toast(terminal, "Terminal reset");
		else
			lds_terminal_toast(terminal, "Unable to reset terminal");
	}
}

void lds_terminal_paste(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (term) {
		if (lds_terminal_vte_paste(term))
			lds_terminal_toast(terminal, "Pasted");
		else
			lds_terminal_toast(terminal, "Clipboard has no text to paste");
	}
}

void lds_terminal_zoom_in(LdsTerminal *terminal) {
	if (!terminal || !lds_terminal_get_current_term(terminal))
		return;
	if (terminal->scale >= LDS_TERMINAL_SCALE_MAX)
		return;

	terminal->scale = lds_terminal_zoom_snap(terminal->scale + 0.1);
	lds_terminal_settings_apply(terminal);
	lds_terminal_toast(terminal, "Zoom in");
}

void lds_terminal_zoom_out(LdsTerminal *terminal) {
	if (!terminal || !lds_terminal_get_current_term(terminal))
		return;
	if (terminal->scale <= LDS_TERMINAL_SCALE_MIN)
		return;

	terminal->scale = lds_terminal_zoom_snap(terminal->scale - 0.1);
	lds_terminal_settings_apply(terminal);
	lds_terminal_toast(terminal, "Zoom out");
}

void lds_terminal_zoom_reset(LdsTerminal *terminal) {
	if (!terminal || !lds_terminal_get_current_term(terminal))
		return;

	terminal->scale = lds_terminal_zoom_snap(1.0);
	lds_terminal_settings_apply(terminal);
	lds_terminal_toast(terminal, "Zoom reset");
}

void lds_terminal_split_vertical(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	if (lds_terminal_vte_split(term, GTK_ORIENTATION_VERTICAL))
		lds_terminal_toast(terminal, "Split down");
	else
		lds_terminal_toast(terminal, "Unable to split pane");
}

void lds_terminal_focus_next_pane(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	lds_terminal_vte_focus_next_pane(term);
}

void lds_terminal_close_pane(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	if (lds_terminal_request_confirm_close_active_pane_if_needed(terminal, term))
		return;

	lds_terminal_close_pane_now(terminal);
}

static void lds_terminal_close_pane_now(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	if (!term)
		return;

	if (lds_terminal_vte_close_active_pane(term))
		lds_terminal_toast(terminal, "Pane closed");
	else
		lds_terminal_toast(terminal, "No split pane to close");
}

void lds_terminal_update_alt_mnemonics(LdsTerminal *terminal) {
	if (!terminal || !terminal->window || !GTK_IS_WINDOW(terminal->window))
		return;

	gtk_window_set_mnemonics_visible(GTK_WINDOW(terminal->window),
									 lds_terminal_settings_alt_enabled());
}
