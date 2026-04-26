/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal tab creation, selection, and lifecycle.
 */

/* Tab management and process shutdown. */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "tabs.h"
#include "lds_terminal.h"
#include "vte.h"
#include "window.h"
#include "internal/lds_terminal_internal.h"
#include "internal/term_registry.h"

static void lds_terminal_update_alt_shortcuts(LdsTerminal *terminal);
static gboolean lds_terminal_kill_later_cb(gpointer data);
static gboolean lds_terminal_tabs_contains_term(LdsTerminal *terminal, LdsTerminalTerm *term);

typedef struct {
	pid_t pid;
	guint64 start_time_ticks;
} LdsTerminalKillLaterData;

static guint64 lds_terminal_read_start_time_ticks(pid_t pid);

/**
 * lds_terminal_tabs_create:
 *
 * Create a terminal term for a new tab.
 */
LdsTerminalTerm *lds_terminal_tabs_create(LdsTerminal *terminal, const char *label,
										  const char *working_directory, char **env, char **exec) {
	if (!terminal)
		return NULL;

	return lds_terminal_vte_create_term(terminal, label, working_directory, env, exec);
}

/**
 * lds_terminal_tabs_append:
 *
 * Append a tab to the tab view.
 */
void lds_terminal_tabs_append(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term)
		return;

	if (term->spawn_failed) {
		lds_terminal_vte_free_term(term);
		if (terminal->terms && terminal->terms->len == 0 && terminal->window)
			gtk_window_destroy(GTK_WINDOW(terminal->window));
		return;
	}

	term->page = adw_tab_view_append(ADW_TAB_VIEW(terminal->tab_view), term->box);
	adw_tab_page_set_title(term->page, "Terminal");
	/* Avoid live VTE snapshots to reduce tab overview overhead. */
	adw_tab_page_set_live_thumbnail(term->page, FALSE);

	g_ptr_array_add(terminal->terms, term);

	term->index = terminal->terms->len - 1;
	adw_tab_view_set_selected_page(ADW_TAB_VIEW(terminal->tab_view), term->page);

	lds_terminal_update_alt_shortcuts(terminal);

	if (term->vte)
		lds_terminal_focus_current_term(terminal);
}

/**
 * lds_terminal_tabs_close:
 *
 * Close a tab with a given reason.
 */
void lds_terminal_tabs_close(LdsTerminal *terminal, LdsTerminalTerm *term,
							 LdsTerminalTabsCloseReason reason) {
	if (!terminal || !term)
		return;
	if (terminal->destroyed)
		return;
	if (!lds_terminal_tabs_contains_term(terminal, term))
		return;

	if (term->closing)
		return;

	if (terminal->tab_view &&
		adw_tab_view_get_selected_page(ADW_TAB_VIEW(terminal->tab_view)) == term->page)
		lds_terminal_cancel_search_debounce(terminal);
	else if (!term->page)
		lds_terminal_cancel_search_debounce(terminal);

	term->closing = TRUE;

	if (term->vte)
		g_signal_handlers_disconnect_by_data(term->vte, term);

	if (reason == LDS_TERMINAL_TABS_CLOSE_BY_USER ||
		reason == LDS_TERMINAL_TABS_CLOSE_BY_SHUTDOWN) {
		if (term->pid > 0) {
			lds_terminal_terminate_child_process(term->pid);
			term->pid = -1;
		}
	}

	if (!term->page) {
		/* Ownership and final free are centralized in lds_terminal_remove_term(). */
		lds_terminal_remove_term(terminal, term);
		return;
	}

	GtkWidget *child = adw_tab_page_get_child(term->page);
	if (child) {
		AdwTabPage *owner = adw_tab_view_get_page(ADW_TAB_VIEW(terminal->tab_view), child);
		if (owner == term->page)
			adw_tab_view_close_page(ADW_TAB_VIEW(terminal->tab_view), term->page);
	}
}

/**
 * lds_terminal_tabs_update_alt:
 *
 * Update Alt shortcut state.
 */
void lds_terminal_tabs_update_alt(LdsTerminal *terminal) {
	lds_terminal_update_alt_shortcuts(terminal);
}

/**
 * lds_terminal_tabs_set_position:
 *
 * Set tab bar position.
 */
void lds_terminal_tabs_set_position(LdsTerminal *terminal, int position) {
	if (!terminal || !terminal->toolbar_view || !terminal->tab_bar)
		return;

	if (position != GTK_POS_TOP && position != GTK_POS_BOTTOM)
		position = GTK_POS_TOP;

	gboolean had_parent = gtk_widget_get_parent(terminal->tab_bar) != NULL;
	if (had_parent) {
		g_object_ref(terminal->tab_bar);
		adw_toolbar_view_remove(ADW_TOOLBAR_VIEW(terminal->toolbar_view), terminal->tab_bar);
	}

	if (position == GTK_POS_BOTTOM) {
		gtk_widget_set_margin_top(terminal->tab_bar, 4);
		gtk_widget_set_margin_bottom(terminal->tab_bar, 0);
		adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(terminal->toolbar_view),
										terminal->tab_bar);
	} else {
		gtk_widget_set_margin_top(terminal->tab_bar, 0);
		gtk_widget_set_margin_bottom(terminal->tab_bar, 4);
		adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(terminal->toolbar_view), terminal->tab_bar);
	}
	if (had_parent)
		g_object_unref(terminal->tab_bar);
}

static void lds_terminal_update_alt_shortcuts(LdsTerminal *terminal) {
	if (!terminal || !terminal->terms)
		return;

	lds_terminal_update_alt_mnemonics(terminal);
}

static gboolean lds_terminal_tabs_contains_term(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term || !terminal->terms)
		return FALSE;

	for (guint i = 0; i < terminal->terms->len; i++) {
		if (g_ptr_array_index(terminal->terms, i) == term)
			return TRUE;
	}

	return FALSE;
}

/**
 * lds_terminal_terminate_child_process:
 *
 * Terminate a child process.
 */
void lds_terminal_terminate_child_process(pid_t pid) {
	if (pid <= 0)
		return;

	LdsTerminalKillLaterData *data = g_new0(LdsTerminalKillLaterData, 1);
	data->pid = pid;
	data->start_time_ticks = lds_terminal_read_start_time_ticks(pid);

	if (kill(pid, SIGTERM) != 0) {
		if (errno != ESRCH)
			g_warning("Failed to terminate child process %d", pid);
		g_free(data);
		return;
	}

	g_timeout_add_seconds(2, lds_terminal_kill_later_cb, data);
}

static gboolean lds_terminal_kill_later_cb(gpointer data) {
	LdsTerminalKillLaterData *kill_data = data;
	if (!kill_data)
		return G_SOURCE_REMOVE;

	if (kill_data->pid > 0 && kill(kill_data->pid, 0) == 0) {
		guint64 current_start_time = lds_terminal_read_start_time_ticks(kill_data->pid);
		gboolean has_identity = kill_data->start_time_ticks > 0 && current_start_time > 0;
		gboolean same_process = has_identity && current_start_time == kill_data->start_time_ticks;
		gboolean allow_fallback_kill = !has_identity;

		if (allow_fallback_kill)
			g_warning("Process identity check unavailable for pid %d; skipping SIGKILL fallback",
					  kill_data->pid);

		if (same_process && kill(kill_data->pid, SIGKILL) != 0 && errno != ESRCH)
			g_warning("Failed to SIGKILL child process %d", kill_data->pid);
	}
	g_free(kill_data);

	return G_SOURCE_REMOVE;
}

static guint64 lds_terminal_read_start_time_ticks(pid_t pid) {
	if (pid <= 0)
		return 0;

	g_autofree gchar *path = g_strdup_printf("/proc/%d/stat", pid);
	char content[4096];
	ssize_t nread = -1;
	int open_flags = O_RDONLY;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
	int fd = open(path, open_flags);
	if (fd < 0)
		return 0;
	nread = read(fd, content, sizeof(content) - 1);
	close(fd);
	if (nread <= 0)
		return 0;
	content[nread] = '\0';

	char *end_comm = strrchr(content, ')');
	if (!end_comm || *(end_comm + 1) == '\0')
		return 0;

	char *fields = end_comm + 2;
	g_auto(GStrv) tokens = g_strsplit(fields, " ", 0);
	if (!tokens || g_strv_length(tokens) <= 19)
		return 0;

	/* /proc/<pid>/stat field 22 (starttime), zero-indexed after "comm" split => tokens[19]. */
	return g_ascii_strtoull(tokens[19], NULL, 10);
}
