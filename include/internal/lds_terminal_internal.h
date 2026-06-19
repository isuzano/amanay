/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Core terminal state and internal wiring.
 */

#ifndef LDS_TERMINAL_INTERNAL_H
#define LDS_TERMINAL_INTERNAL_H

/* Concrete state shared across internal modules. */

#include <gio/gio.h>
#include <gtk/gtk.h>
#include "lds_terminal.h"
#include "internal/link_cache.h"
#include "internal/search_state.h"

#define LDS_TERMINAL_DISPLAY_NAME "Amanay"

struct _LdsTerminal {
	LdsTerminalState *parent;

	GtkWidget *window;
	GtkWidget *box;
	GtkWidget *menu;
	GtkWidget *toolbar_view;
	GtkWidget *tab_view;
	GtkWidget *tab_bar;
	GtkWidget *tab_add_button;
	GtkWidget *tab_overview;
	GtkWidget *overview_button;
	GtkWidget *overview_label;
	gboolean overview_search_forced;
	GtkWidget *title_widget;
	GtkWidget *toast_overlay;
	GtkWidget *breakpoint_bin;
	GtkWidget *search_popover;
	GtkWidget *search_button;
	GtkWidget *search_entry;
	GtkWidget *search_match_case;
	GtkWidget *search_regex;
	GtkWidget *search_whole_word;
	GtkWidget *search_wrap;
	GtkWidget *search_next;
	GtkWidget *search_prev;
	GtkWidget *search_count_label;
	LdsTerminalSearchState *search_state;
	GtkEventController *window_key_controller;
	GtkEventController *search_key_controller;
	GSimpleActionGroup *window_actions;
	GSimpleActionGroup *menu_actions_cached_from;
	GAction *action_copy;
	GAction *action_paste;
	GAction *action_clear;
	GAction *action_reset;
	GAction *action_rename_tab;
	GAction *action_zoom_in;
	GAction *action_zoom_out;
	GAction *action_zoom_reset;
	GAction *action_split_vertical;
	GAction *action_close_pane;
	GAction *action_focus_next_pane;
	struct _LdsTerminalTerm *menu_edit_state_term;
	guint menu_edit_state_bits;
	gboolean menu_edit_state_valid;
	struct _LdsTerminalTerm *menu_paste_cache_term;
	gboolean menu_paste_cache_valid;
	gboolean menu_paste_enabled_cache;
	struct _LdsTerminalTerm *current_selected_term;
	guint64 current_term_lookup_calls;
	guint64 current_term_lookup_fast_hits;
	guint64 current_term_lookup_fallback_scans;
	gint current_tab_position;
	gint startup_geometry_bitmask;
	guint startup_geometry_columns;
	guint startup_geometry_rows;
	gint startup_geometry_xoff;
	gint startup_geometry_yoff;
	gboolean close_confirm_visible;
	gboolean close_confirm_accepted;
	guint close_async_token;
	guint focus_recovery_idle_id;

	GPtrArray *terms;
	gdouble scale;
	gboolean destroyed;
	gboolean destroy_scheduled;
	guint destroy_idle_id;
};

struct _LdsTerminalState {
	GtkApplication *app;
	GPtrArray *windows;
	LdsTerminalCommandArgs args;
};

struct _LdsTerminalTerm {
	LdsTerminal *parent;
	GtkWidget *box;
	GtkWidget *vte;
	GtkWidget *pane_split;
	GtkWidget *secondary_vte;
	GtkWidget *context_menu;
	GtkWidget *context_source_vte;
	AdwTabPage *page;
	GSimpleActionGroup *context_actions;
	LdsLinkLineCache *link_cache;
	guint64 link_cache_generation;
	gchar *context_link_target;
	double context_x;
	double context_y;
	pid_t pid;
	pid_t secondary_pid;
	gint index;
	guint post_close_focus_token;
	guint post_close_focus_idle_id;
	guint post_close_focus_timeout_id;
	gboolean closing;
	gboolean spawn_failed;
	gchar *custom_tab_title;
};

static inline LdsTerminalSearchState *lds_terminal_search_state_ensure(LdsTerminal *terminal) {
	if (!terminal)
		return NULL;

	if (!terminal->search_state)
		terminal->search_state = lds_terminal_search_state_new();

	return terminal->search_state;
}

#define LDS_TERMINAL_WINDOW_DATA_KEY "lds-terminal-instance"

/* Window management */
void lds_terminal_window_initialize(LdsTerminal *terminal);
void lds_terminal_window_close(LdsTerminal *terminal);
gboolean lds_terminal_window_should_confirm_close(LdsTerminal *terminal);
gboolean lds_terminal_window_confirm_close(LdsTerminal *terminal);
void lds_terminal_window_update_title(LdsTerminal *terminal, const char *title);

/* Tabs */
LdsTerminalTerm *lds_terminal_tabs_create(LdsTerminal *terminal, const char *label,
										  const char *working_directory, char **env, char **exec);
void lds_terminal_tabs_append(LdsTerminal *terminal, LdsTerminalTerm *term);

typedef enum {
	LDS_TERMINAL_TABS_CLOSE_BY_USER,
	LDS_TERMINAL_TABS_CLOSE_BY_CHILD,
	LDS_TERMINAL_TABS_CLOSE_BY_EOF,
	LDS_TERMINAL_TABS_CLOSE_BY_SHUTDOWN,
	LDS_TERMINAL_TABS_CLOSE_BY_SPAWN_ERROR
} LdsTerminalTabsCloseReason;

void lds_terminal_tabs_close(LdsTerminal *terminal, LdsTerminalTerm *term,
							 LdsTerminalTabsCloseReason reason);
void lds_terminal_tabs_update_alt(LdsTerminal *terminal);
void lds_terminal_tabs_set_position(LdsTerminal *terminal, int position);
void lds_terminal_terminate_child_process(pid_t pid);
void lds_terminal_remove_term(LdsTerminal *terminal, LdsTerminalTerm *term);

/* VTE */
LdsTerminalTerm *lds_terminal_vte_create_term(LdsTerminal *terminal, const char *label,
											  const char *cwd, char **env, char **exec);
void lds_terminal_vte_free_term(LdsTerminalTerm *term);
void lds_terminal_vte_apply_settings(LdsTerminal *terminal, LdsTerminalTerm *term);
gboolean lds_terminal_vte_copy(LdsTerminalTerm *term);
gboolean lds_terminal_vte_paste(LdsTerminalTerm *term);
gboolean lds_terminal_vte_clear(LdsTerminalTerm *term);
gboolean lds_terminal_vte_reset(LdsTerminalTerm *term);
void lds_terminal_vte_clear_cached_font_desc(void);
void lds_terminal_vte_handle_spawn_ready(LdsTerminalTerm *term, GPid pid, GError *error);
gboolean lds_terminal_vte_split(LdsTerminalTerm *term, GtkOrientation orientation);
gboolean lds_terminal_vte_close_active_pane(LdsTerminalTerm *term);
void lds_terminal_vte_focus_next_pane(LdsTerminalTerm *term);
gboolean lds_terminal_vte_active_has_selection(LdsTerminalTerm *term);
gboolean lds_terminal_vte_active_clipboard_has_text(LdsTerminalTerm *term);
gboolean lds_terminal_vte_has_split(LdsTerminalTerm *term);
gboolean lds_terminal_vte_active_has_running_job(LdsTerminalTerm *term);
gboolean lds_terminal_vte_term_has_running_jobs(LdsTerminalTerm *term);
guint lds_terminal_vte_term_running_job_count(LdsTerminalTerm *term);
void lds_terminal_vte_resync_layout(LdsTerminalTerm *term);
void lds_terminal_vte_sync_context_actions(LdsTerminalTerm *term);
void lds_terminal_vte_refresh_tab_label(LdsTerminalTerm *term);

/* Menu */
void lds_terminal_menu_initialize(LdsTerminal *terminal);
void lds_terminal_menu_refresh_popover(LdsTerminal *terminal);
void lds_terminal_menu_update_accelerators(LdsTerminal *terminal);
void lds_terminal_menu_invalidate_clipboard_cache(LdsTerminal *terminal);
gboolean lds_terminal_menu_clipboard_has_text_cached(LdsTerminal *terminal, LdsTerminalTerm *term);
void lds_terminal_menu_sync_edit_actions(LdsTerminal *terminal);
void lds_terminal_prefs_show(LdsTerminal *terminal);

/* Shared internal helpers */
void lds_terminal_update_alt_mnemonics(LdsTerminal *terminal);
void lds_terminal_focus_current_term(LdsTerminal *terminal);
void lds_terminal_schedule_focus_current_term(LdsTerminal *terminal);
LdsTerminalTerm *lds_terminal_get_current_term(LdsTerminal *terminal);
LdsTerminalTerm *lds_terminal_find_term_by_page(LdsTerminal *terminal, AdwTabPage *page,
												gboolean skip_closing);
void lds_terminal_toast(LdsTerminal *terminal, const char *text);
gboolean lds_terminal_overview_should_consume_enter(guint keyval, gboolean search_active);
void lds_terminal_overview_update_search_gating(LdsTerminal *terminal);
gboolean lds_terminal_overview_maybe_force_search_on_key(LdsTerminal *terminal, guint keyval,
														 GdkModifierType state);
guint lds_terminal_search_compute_debounce_ms(gboolean regex_enabled, guint scrollback_lines);
guint lds_terminal_search_count_matches_for_query(const char *haystack, const char *query,
												  gboolean match_case, gboolean use_regex,
												  gboolean whole_word, gboolean *valid_regex);
gboolean lds_terminal_search_build_regex(const char *query, gboolean match_case, gboolean use_regex,
										 gboolean whole_word, GRegex **regex_out,
										 gboolean *valid_regex);
guint lds_terminal_search_count_matches_with_regex(const char *haystack, GRegex *regex,
												   guint max_matches, gboolean *truncated,
												   GCancellable *cancellable);
gboolean lds_terminal_search_count_request_is_duplicate(LdsTerminal *terminal,
														LdsTerminalTerm *term, const char *query,
														guint opt_flags);
void lds_terminal_search_read_options(LdsTerminal *terminal, gboolean *match_case,
									  gboolean *use_regex, gboolean *whole_word, gboolean *wrap,
									  guint *opt_flags);
void lds_terminal_search_schedule_debounce(LdsTerminal *terminal, GSourceFunc callback);
gboolean lds_terminal_validate_initial_term(LdsTerminal *terminal, LdsTerminalTerm *term);
void lds_terminal_cancel_search_debounce(LdsTerminal *terminal);
void lds_terminal_search_notify_contents_changed(LdsTerminal *terminal, LdsTerminalTerm *term);

#endif /* LDS_TERMINAL_INTERNAL_H */
