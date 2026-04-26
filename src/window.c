/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Main window lifecycle and close handling.
 */

/* Window lifecycle and confirmation. */

#include <adwaita.h>
#include <gtk/gtk.h>

#include "window.h"
#include "settings.h"
#include "internal/diag.h"
#include "internal/lds_terminal_internal.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

#ifndef GDK_HINT_POS
/* Removed from GTK4 headers; kept for geometry bitmask compatibility. */
#define GDK_HINT_POS 0x02
#endif

static gboolean lds_terminal_on_close_request(GtkWindow *window, LdsTerminal *terminal);
static void lds_terminal_on_destroy(GtkWidget *widget, LdsTerminal *terminal);
static gboolean lds_terminal_on_destroy_idle(gpointer data);
static void lds_terminal_on_confirm_close_response(AdwAlertDialog *dialog, const char *response,
												   LdsTerminal *terminal);
static void lds_terminal_on_map(GtkWidget *widget, LdsTerminal *terminal);
static void lds_terminal_window_store_geometry(LdsTerminal *terminal);
static void lds_terminal_window_restore_position(LdsTerminal *terminal);
static void lds_terminal_window_prepare_close(LdsTerminal *terminal);
static void lds_terminal_window_nullify_widget_refs(LdsTerminal *terminal);

/**
 * lds_terminal_window_initialize:
 *
 * Initialize window signals and state.
 */
void lds_terminal_window_initialize(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;

	g_signal_connect(terminal->window, "close-request", G_CALLBACK(lds_terminal_on_close_request),
					 terminal);

	g_signal_connect(terminal->window, "destroy", G_CALLBACK(lds_terminal_on_destroy), terminal);

	g_signal_connect(terminal->window, "map", G_CALLBACK(lds_terminal_on_map), terminal);
}

/**
 * lds_terminal_window_close:
 *
 * Close the window explicitly.
 */
void lds_terminal_window_close(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;
	gtk_window_close(GTK_WINDOW(terminal->window));
}

gboolean lds_terminal_window_should_confirm_close(LdsTerminal *terminal) {
	if (!terminal || !terminal->window || !terminal->terms)
		return FALSE;

	gboolean jobs_running = FALSE;
	if (terminal->tab_view) {
		for (guint i = 0; i < terminal->terms->len; i++) {
			LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
			if (lds_terminal_vte_term_has_running_jobs(term)) {
				jobs_running = TRUE;
				break;
			}
		}
	}

	if (jobs_running && lds_terminal_settings_confirm_running_process_enabled())
		return TRUE;

	return lds_terminal_settings_confirm_close_enabled() && terminal->terms->len > 1;
}

/**
 * lds_terminal_window_confirm_close:
 *
 * Request confirmation if needed.
 */
gboolean lds_terminal_window_confirm_close(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return TRUE;

	if (terminal->close_confirm_accepted) {
		terminal->close_confirm_accepted = FALSE;
		return TRUE;
	}

	if (!lds_terminal_window_should_confirm_close(terminal))
		return TRUE;

	if (terminal->close_confirm_visible)
		return FALSE;

	guint jobs = 0;
	if (terminal->tab_view) {
		for (guint i = 0; i < terminal->terms->len; i++) {
			LdsTerminalTerm *term = g_ptr_array_index(terminal->terms, i);
			jobs += lds_terminal_vte_term_running_job_count(term);
		}
	}

	g_autofree gchar *body = NULL;
	if (jobs > 0) {
		body = g_strdup_printf("This will close %u tabs and terminate %u running foreground jobs. "
							   "Continue?",
							   terminal->terms->len, jobs);
	} else {
		body = g_strdup_printf("You are about to close %u terminal tabs. Continue?",
							   terminal->terms->len);
	}

	AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Confirm close", body));
	adw_alert_dialog_add_response(dialog, "cancel", "Cancel");
	adw_alert_dialog_add_response(dialog, "close", "Close");
	adw_alert_dialog_set_response_appearance(dialog, "close", ADW_RESPONSE_DESTRUCTIVE);
	adw_alert_dialog_set_default_response(dialog, "cancel");
	adw_alert_dialog_set_close_response(dialog, "cancel");

	terminal->close_confirm_visible = TRUE;
	g_signal_connect(dialog, "response", G_CALLBACK(lds_terminal_on_confirm_close_response),
					 terminal);
	adw_dialog_present(ADW_DIALOG(dialog), terminal->window);
	return FALSE;
}

/**
 * lds_terminal_window_update_title:
 *
 * Update the window title.
 */
void lds_terminal_window_update_title(LdsTerminal *terminal, const char *title) {
	if (!terminal || !terminal->window)
		return;

	gtk_window_set_title(GTK_WINDOW(terminal->window), title ? title : LDS_TERMINAL_DISPLAY_NAME);

	if (terminal->title_widget && ADW_IS_WINDOW_TITLE(terminal->title_widget)) {
		adw_window_title_set_subtitle(ADW_WINDOW_TITLE(terminal->title_widget), title ? title : "");
	}
}

static void lds_terminal_on_destroy(GtkWidget *widget, LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed || terminal->destroy_scheduled)
		return;

	lds_terminal_diag_log("window", "window:destroy");

	if (widget && GTK_IS_WIDGET(widget))
		g_object_set_data(G_OBJECT(widget), LDS_TERMINAL_WINDOW_DATA_KEY, NULL);

	lds_terminal_window_store_geometry(terminal);
	terminal->destroy_scheduled = TRUE;
	terminal->close_async_token++;
	terminal->close_confirm_visible = FALSE;
	terminal->close_confirm_accepted = FALSE;

	/* Avoid widget/object mutations during GTK destroy accounting. */
	lds_terminal_window_nullify_widget_refs(terminal);

	terminal->destroy_idle_id =
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, lds_terminal_on_destroy_idle, terminal, NULL);
}

static gboolean lds_terminal_on_destroy_idle(gpointer data) {
	LdsTerminal *terminal = data;
	if (!terminal || terminal->destroyed)
		return G_SOURCE_REMOVE;

	terminal->destroy_idle_id = 0;
	lds_terminal_destroy(terminal);
	return G_SOURCE_REMOVE;
}

static gboolean lds_terminal_on_close_request(GtkWindow *window, LdsTerminal *terminal) {
	(void)window;
	if (lds_terminal_window_confirm_close(terminal)) {
		lds_terminal_window_prepare_close(terminal);
		return FALSE;
	}
	return TRUE;
}

static void lds_terminal_on_confirm_close_response(AdwAlertDialog *dialog, const char *response,
												   LdsTerminal *terminal) {
	(void)dialog;
	if (!terminal)
		return;

	terminal->close_confirm_visible = FALSE;
	if (g_strcmp0(response, "close") != 0)
		return;

	terminal->close_confirm_accepted = TRUE;
	lds_terminal_window_prepare_close(terminal);
	if (terminal->window)
		gtk_window_close(GTK_WINDOW(terminal->window));
}

static void lds_terminal_window_prepare_close(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed)
		return;

	if (terminal->search_popover && GTK_IS_POPOVER(terminal->search_popover))
		gtk_popover_popdown(GTK_POPOVER(terminal->search_popover));

	if (terminal->menu && GTK_IS_MENU_BUTTON(terminal->menu))
		gtk_menu_button_popdown(GTK_MENU_BUTTON(terminal->menu));

	if (terminal->tab_overview && ADW_IS_TAB_OVERVIEW(terminal->tab_overview))
		adw_tab_overview_set_open(ADW_TAB_OVERVIEW(terminal->tab_overview), FALSE);

	if (terminal->overview_button && GTK_IS_TOGGLE_BUTTON(terminal->overview_button))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(terminal->overview_button), FALSE);
}

static void lds_terminal_window_nullify_widget_refs(LdsTerminal *terminal) {
	if (!terminal)
		return;

	terminal->window = NULL;
	terminal->box = NULL;
	terminal->menu = NULL;
	terminal->toolbar_view = NULL;
	terminal->tab_view = NULL;
	terminal->tab_bar = NULL;
	terminal->tab_add_button = NULL;
	terminal->tab_overview = NULL;
	terminal->overview_button = NULL;
	terminal->overview_label = NULL;
	terminal->title_widget = NULL;
	terminal->toast_overlay = NULL;
	terminal->breakpoint_bin = NULL;
	terminal->search_popover = NULL;
	terminal->search_button = NULL;
	terminal->search_entry = NULL;
	terminal->search_match_case = NULL;
	terminal->search_regex = NULL;
	terminal->search_whole_word = NULL;
	terminal->search_wrap = NULL;
	terminal->search_next = NULL;
	terminal->search_prev = NULL;
	terminal->search_count_label = NULL;
	terminal->window_key_controller = NULL;
	terminal->search_key_controller = NULL;
}

static void lds_terminal_on_map(GtkWidget *widget, LdsTerminal *terminal) {
	(void)widget;
	lds_terminal_window_restore_position(terminal);
	g_signal_handlers_disconnect_by_func(widget, G_CALLBACK(lds_terminal_on_map), terminal);
}

static void lds_terminal_window_store_geometry(LdsTerminal *terminal) {
	if (!terminal || !terminal->window)
		return;

	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(terminal->window));
	if (!surface)
		return;

	gint width = gdk_surface_get_width(surface);
	gint height = gdk_surface_get_height(surface);
	/* Persist only size; position restore is compositor/WM controlled and unreliable. */
	lds_terminal_settings_set_window_geometry(width, height, -1, -1);
}

static void lds_terminal_window_restore_position(LdsTerminal *terminal) {
	(void)terminal;
	lds_terminal_diag_log("window", "restore-position disabled: restoring size only");
}
