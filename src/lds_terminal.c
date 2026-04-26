/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Main terminal window orchestration.
 */

/* Core terminal window orchestration. */

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gio/gio.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

#ifndef GDK_HINT_POS
#define GDK_HINT_POS 0x02
#endif

#ifndef GDK_HINT_SIZE
#define GDK_HINT_SIZE 0x04
#endif

#include <glib.h>
#include <string.h>
#include <vte/vte.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "lds_terminal.h"
#include "internal/lds_terminal_internal.h"
#include "internal/diag.h"
#include "internal/overview_controller.h"
#include "internal/search_engine.h"
#include "internal/search_ui.h"
#include "internal/term_registry.h"
#include "internal/vte_compat.h"

#include "menu.h"
#include "tabs.h"
#include "vte.h"
#include "settings.h"
#include "window.h"

static gboolean lds_terminal_on_window_key_press(GtkEventControllerKey *controller, guint keyval,
												 guint keycode, GdkModifierType state,
												 LdsTerminal *terminal);
static gboolean lds_terminal_on_tab_close_request(AdwTabView *view, AdwTabPage *page,
												  LdsTerminal *terminal);
static void lds_terminal_on_tab_close_confirm_response(AdwAlertDialog *dialog, const char *response,
													   gpointer user_data);
static void lds_terminal_on_style_dark_notify(AdwStyleManager *manager, GParamSpec *pspec,
											  LdsTerminal *terminal);
static GSettings *lds_terminal_get_settings(void);
static gboolean lds_terminal_settings_key_is_global(const char *key);
static gboolean lds_terminal_settings_key_is_local_vte(const char *key);
static void lds_terminal_on_global_settings_changed(GSettings *settings, gchar *key,
													LdsTerminal *terminal);
static gboolean lds_terminal_on_overview_key_press(GtkEventControllerKey *controller, guint keyval,
												   guint keycode, GdkModifierType state,
												   LdsTerminal *terminal);
static void lds_terminal_on_new_tab_button_clicked(GtkButton *button, LdsTerminal *terminal);
static AdwTabView *lds_terminal_on_tab_create_window(AdwTabView *view, LdsTerminal *terminal);
static void lds_terminal_on_clipboard_changed(GdkClipboard *clipboard, LdsTerminal *terminal);
static void lds_terminal_apply_icon_theme_defaults(void);
static gboolean lds_terminal_shortcut_search_next(GtkWidget *widget, GVariant *args,
												  gpointer user_data);
static gboolean lds_terminal_shortcut_search_prev(GtkWidget *widget, GVariant *args,
												  gpointer user_data);
static gboolean lds_terminal_shortcut_overview(GtkWidget *widget, GVariant *args,
											   gpointer user_data);
static void lds_terminal_shortcuts_init(LdsTerminal *terminal);
static void lds_terminal_setup_breakpoints(LdsTerminal *terminal);
static LdsTerminal *lds_terminal_create_internal(LdsTerminalState *parent,
												 LdsTerminalCommandArgs *args,
												 gboolean with_initial_tab);
static void lds_terminal_window_setup_base(LdsTerminal *terminal);
static AdwHeaderBar *lds_terminal_build_window_shell(LdsTerminal *terminal);
static void lds_terminal_setup_tab_surface(LdsTerminal *terminal);
static void lds_terminal_setup_header_controls(LdsTerminal *terminal, AdwHeaderBar *header);
static LdsTerminalTerm *lds_terminal_create_initial_tab(LdsTerminal *terminal,
														 LdsTerminalCommandArgs *args);

typedef struct {
	GWeakRef window_ref;
	guint close_async_token;
	AdwTabPage *page;
} LdsTerminalTabCloseConfirmData;

static void lds_terminal_apply_icon_theme_defaults(void) {
	GtkSettings *settings = gtk_settings_get_default();
	if (!settings)
		return;

	if (!g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-icon-theme-name"))
		return;

	g_autofree gchar *icon_theme = NULL;
	g_object_get(settings, "gtk-icon-theme-name", &icon_theme, NULL);
	if (g_strcmp0(icon_theme, "Adwaita") == 0)
		return;

	g_object_set(settings, "gtk-icon-theme-name", "Adwaita", NULL);
}

static void lds_terminal_window_setup_base(LdsTerminal *terminal) {
	gint width = 0;
	gint height = 0;
	GdkDisplay *display = NULL;

	if (!terminal || !terminal->window)
		return;

	width = lds_terminal_settings_window_width();
	height = lds_terminal_settings_window_height();

	if (width <= 0)
		width = 800;
	if (height <= 0)
		height = 500;

	gtk_window_set_default_size(GTK_WINDOW(terminal->window), width, height);
	gtk_window_set_title(GTK_WINDOW(terminal->window), LDS_TERMINAL_DISPLAY_NAME);
	gtk_window_set_icon_name(GTK_WINDOW(terminal->window), "lds-terminal");

	display = gdk_display_get_default();

#ifdef GDK_WINDOWING_X11
	if (display && GDK_IS_X11_DISPLAY(display)) {
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		/* Keep the X11 WM_CLASS aligned with the desktop entry/app identity. */
		gdk_x11_display_set_program_class(display, "LdsTerminal");
		G_GNUC_END_IGNORE_DEPRECATIONS
	}
#endif
}

static AdwHeaderBar *lds_terminal_build_window_shell(LdsTerminal *terminal) {
	AdwToolbarView *view = NULL;
	AdwHeaderBar *header = NULL;
	GtkWidget *breakpoint_bin = NULL;
	GtkWidget *title = NULL;
	AdwToastOverlay *overlay = NULL;
	AdwTabOverview *overview = NULL;
	GtkEventController *overview_key = NULL;

	if (!terminal || !terminal->window)
		return NULL;

	view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
	terminal->toolbar_view = GTK_WIDGET(view);

	breakpoint_bin = adw_breakpoint_bin_new();
	terminal->breakpoint_bin = breakpoint_bin;
	gtk_widget_set_size_request(breakpoint_bin, 600, 400);
	adw_breakpoint_bin_set_child(ADW_BREAKPOINT_BIN(breakpoint_bin), GTK_WIDGET(view));

	if (ADW_IS_APPLICATION_WINDOW(terminal->window)) {
		adw_application_window_set_content(ADW_APPLICATION_WINDOW(terminal->window),
										   breakpoint_bin);
	} else if (ADW_IS_WINDOW(terminal->window)) {
		adw_window_set_content(ADW_WINDOW(terminal->window), breakpoint_bin);
	} else {
		gtk_window_set_child(GTK_WINDOW(terminal->window), breakpoint_bin);
	}

	adw_toolbar_view_set_top_bar_style(view, ADW_TOOLBAR_RAISED_BORDER);
	adw_toolbar_view_set_bottom_bar_style(view, ADW_TOOLBAR_RAISED_BORDER);

	header = ADW_HEADER_BAR(adw_header_bar_new());
	adw_header_bar_set_show_start_title_buttons(header, FALSE);
	adw_header_bar_set_show_end_title_buttons(header, TRUE);

	title = adw_window_title_new(LDS_TERMINAL_DISPLAY_NAME, "");
	terminal->title_widget = title;
	adw_header_bar_set_title_widget(header, title);
	adw_toolbar_view_add_top_bar(view, GTK_WIDGET(header));

	terminal->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
	adw_toast_overlay_set_child(overlay, terminal->box);
	terminal->toast_overlay = GTK_WIDGET(overlay);

	overview = ADW_TAB_OVERVIEW(adw_tab_overview_new());
	gtk_widget_add_css_class(GTK_WIDGET(overview), "lds-tab-overview");
	adw_tab_overview_set_child(overview, GTK_WIDGET(overlay));
	adw_tab_overview_set_enable_search(overview, FALSE);
	adw_tab_overview_set_enable_new_tab(overview, FALSE);
	terminal->tab_overview = GTK_WIDGET(overview);

	g_signal_connect(overview, "notify::open", G_CALLBACK(lds_terminal_on_overview_open_notify),
					 terminal);
	g_signal_connect(overview, "create-tab", G_CALLBACK(lds_terminal_on_overview_create_tab),
					 terminal);

	overview_key = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(overview_key, GTK_PHASE_CAPTURE);
	g_signal_connect(overview_key, "key-pressed",
					 G_CALLBACK(lds_terminal_on_overview_key_press), terminal);
	gtk_widget_add_controller(GTK_WIDGET(overview), overview_key);

	adw_toolbar_view_set_content(view, GTK_WIDGET(overview));

	return header;
}

static void lds_terminal_setup_tab_surface(LdsTerminal *terminal) {
	GtkGesture *tab_press = NULL;
	GdkDisplay *display = NULL;
	GdkClipboard *clipboard = NULL;
	GtkEventController *key = NULL;
	GSettings *settings = NULL;

	if (!terminal || !terminal->window || !terminal->box)
		return;

	lds_terminal_window_initialize(terminal);

	key = gtk_event_controller_key_new();
	terminal->window_key_controller = key;
	gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
	g_signal_connect(key, "key-pressed", G_CALLBACK(lds_terminal_on_window_key_press), terminal);
	gtk_widget_add_controller(terminal->window, key);

	lds_terminal_shortcuts_init(terminal);

	g_signal_connect(adw_style_manager_get_default(), "notify::dark",
					 G_CALLBACK(lds_terminal_on_style_dark_notify), terminal);

	settings = lds_terminal_get_settings();
	if (settings) {
		g_signal_connect(settings, "changed",
						 G_CALLBACK(lds_terminal_on_global_settings_changed), terminal);
	}

	terminal->tab_view = GTK_WIDGET(adw_tab_view_new());
	terminal->tab_bar = GTK_WIDGET(adw_tab_bar_new());
	if (terminal->tab_overview) {
		adw_tab_overview_set_view(ADW_TAB_OVERVIEW(terminal->tab_overview),
								  ADW_TAB_VIEW(terminal->tab_view));
	}

	adw_tab_bar_set_view(ADW_TAB_BAR(terminal->tab_bar), ADW_TAB_VIEW(terminal->tab_view));
	adw_tab_bar_set_autohide(ADW_TAB_BAR(terminal->tab_bar), TRUE);
	adw_tab_bar_set_expand_tabs(ADW_TAB_BAR(terminal->tab_bar), TRUE);

	tab_press = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_press), GDK_BUTTON_PRIMARY);
	g_signal_connect(tab_press, "pressed", G_CALLBACK(lds_terminal_on_tab_bar_pressed), terminal);
	gtk_widget_add_controller(terminal->tab_bar, GTK_EVENT_CONTROLLER(tab_press));

	adw_tab_view_add_shortcuts(
		ADW_TAB_VIEW(terminal->tab_view),
		ADW_TAB_VIEW_SHORTCUT_CONTROL_TAB | ADW_TAB_VIEW_SHORTCUT_CONTROL_SHIFT_TAB |
			ADW_TAB_VIEW_SHORTCUT_CONTROL_PAGE_UP | ADW_TAB_VIEW_SHORTCUT_CONTROL_PAGE_DOWN |
			ADW_TAB_VIEW_SHORTCUT_CONTROL_SHIFT_PAGE_UP |
			ADW_TAB_VIEW_SHORTCUT_CONTROL_SHIFT_PAGE_DOWN);

	g_signal_connect(terminal->tab_view, "close-page",
					 G_CALLBACK(lds_terminal_on_tab_close_request), terminal);
	g_signal_connect(terminal->tab_view, "notify::n-pages",
					 G_CALLBACK(lds_terminal_on_tab_pages_changed), terminal);
	g_signal_connect(terminal->tab_view, "notify::selected-page",
					 G_CALLBACK(lds_terminal_on_tab_selected_page_changed), terminal);
	g_signal_connect(terminal->tab_view, "create-window",
					 G_CALLBACK(lds_terminal_on_tab_create_window), terminal);
	g_signal_connect(terminal->tab_view, "page-detached",
					 G_CALLBACK(lds_terminal_on_tab_page_detached), terminal);
	g_signal_connect(terminal->tab_view, "page-attached",
					 G_CALLBACK(lds_terminal_on_tab_page_attached), terminal);

	gtk_box_append(GTK_BOX(terminal->box), terminal->tab_view);
	gtk_widget_set_hexpand(terminal->tab_view, TRUE);
	gtk_widget_set_vexpand(terminal->tab_view, TRUE);

	gtk_widget_set_margin_start(terminal->tab_bar, 6);
	gtk_widget_set_margin_end(terminal->tab_bar, 6);

	lds_terminal_menu_initialize(terminal);

	display = gtk_widget_get_display(terminal->window);
	if (display) {
		clipboard = gdk_display_get_clipboard(display);
		if (clipboard) {
			g_signal_connect(clipboard, "changed",
							 G_CALLBACK(lds_terminal_on_clipboard_changed), terminal);
		}
	}

	lds_terminal_search_dialog_init(terminal);
}

static void lds_terminal_setup_header_controls(LdsTerminal *terminal, AdwHeaderBar *header) {
	GtkWidget *tab_add = NULL;
	GtkWidget *divider = NULL;
	GtkWidget *end_divider = NULL;
	GtkWidget *overview_label = NULL;
	GtkWidget *overview_button = NULL;
	GtkWidget *overview_icon = NULL;

	if (!terminal || !header)
		return;

	tab_add = gtk_button_new_from_icon_name("list-add-symbolic");
	gtk_widget_add_css_class(tab_add, "flat");
	gtk_widget_set_tooltip_text(tab_add, _("New Tab"));
	g_signal_connect(tab_add, "clicked", G_CALLBACK(lds_terminal_on_new_tab_button_clicked),
					 terminal);

	terminal->tab_add_button = tab_add;
	gtk_widget_set_margin_start(terminal->tab_add_button, 6);
	adw_header_bar_pack_start(header, terminal->tab_add_button);

	if (terminal->tab_add_button && terminal->search_button) {
		divider = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		gtk_widget_set_margin_start(divider, 4);
		gtk_widget_set_margin_end(divider, 4);
		adw_header_bar_pack_start(header, divider);
	}

	if (terminal->search_button) {
		gtk_widget_set_margin_start(terminal->search_button, 0);
		adw_header_bar_pack_start(header, terminal->search_button);
	}

	if (terminal->menu) {
		gtk_widget_set_margin_end(terminal->menu, 6);
		adw_header_bar_pack_end(header, terminal->menu);
	}

	if (terminal->menu) {
		end_divider = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		gtk_widget_set_margin_start(end_divider, 4);
		gtk_widget_set_margin_end(end_divider, 4);
		adw_header_bar_pack_end(header, end_divider);
	}

	overview_label = gtk_label_new(NULL);
	gtk_widget_add_css_class(overview_label, "dim-label");
	gtk_widget_set_margin_end(overview_label, 6);
	terminal->overview_label = overview_label;

	overview_button = gtk_toggle_button_new();
	overview_icon = gtk_image_new_from_icon_name("view-grid-symbolic");
	gtk_button_set_child(GTK_BUTTON(overview_button), overview_icon);
	gtk_widget_add_css_class(overview_button, "flat");
	gtk_widget_set_tooltip_text(overview_button, _("Tabs Overview"));
	g_signal_connect(overview_button, "toggled",
					 G_CALLBACK(lds_terminal_on_overview_button_toggled), terminal);
	terminal->overview_button = overview_button;

	adw_header_bar_pack_end(header, overview_label);
	adw_header_bar_pack_end(header, overview_button);
}

static LdsTerminalTerm *lds_terminal_create_initial_tab(LdsTerminal *terminal,
														 LdsTerminalCommandArgs *args) {
	LdsTerminalTerm *term = NULL;

	if (!terminal || !args)
		return NULL;

	lds_terminal_diag_log("create", "creating first tab");
	term = lds_terminal_tabs_create(terminal, args->title, args->working_directory, NULL,
									args->command);
	lds_terminal_diag_log("create", "first tab create result=%p", (void *)term);

	if (!lds_terminal_validate_initial_term(terminal, term))
		return NULL;

	if ((args->geometry_bitmask & GDK_HINT_SIZE) && term->vte) {
		guint columns = args->geometry_columns > 0 ? args->geometry_columns : 80u;
		guint rows = args->geometry_rows > 0 ? args->geometry_rows : 24u;
		vte_terminal_set_size(VTE_TERMINAL(term->vte), (glong)columns, (glong)rows);
	}

	lds_terminal_tabs_append(terminal, term);
	lds_terminal_diag_log("create", "first tab appended");

	if (term->page)
		lds_terminal_window_update_title(terminal, adw_tab_page_get_title(term->page));

	return term;
}

/**
 * lds_terminal_create:
 *
 * Create a terminal window.
 */
static LdsTerminal *lds_terminal_create_internal(LdsTerminalState *parent,
												 LdsTerminalCommandArgs *args,
												 gboolean with_initial_tab) {
	lds_terminal_diag_log("create", "begin");
	if (!parent || !parent->windows)
		return NULL;

	LdsTerminalCommandArgs empty_args = {0};
	if (!args)
		args = &empty_args;

	lds_terminal_apply_icon_theme_defaults();

	LdsTerminal *terminal = g_new0(LdsTerminal, 1);

	terminal->parent = parent;
	terminal->terms = g_ptr_array_new_with_free_func((GDestroyNotify)lds_terminal_vte_free_term);
	terminal->scale = 1.0;
	terminal->search_last_valid_regex = TRUE;
	terminal->search_haystack_generation = 1;
	terminal->search_count_regex_valid = TRUE;
	terminal->search_pending_match_case = TRUE;
	terminal->current_tab_position = -1;
	terminal->startup_geometry_bitmask = args ? args->geometry_bitmask : 0;
	terminal->startup_geometry_columns = args ? args->geometry_columns : 0;
	terminal->startup_geometry_rows = args ? args->geometry_rows : 0;
	terminal->startup_geometry_xoff = args ? args->geometry_xoff : -1;
	terminal->startup_geometry_yoff = args ? args->geometry_yoff : -1;

	/* Register window in global state before signal wiring. */
	g_ptr_array_add(parent->windows, terminal);

	if (parent && parent->app)
		terminal->window = adw_application_window_new(parent->app);
	else
		terminal->window = adw_window_new();
	g_object_set_data(G_OBJECT(terminal->window), LDS_TERMINAL_WINDOW_DATA_KEY, terminal);

	lds_terminal_diag_log("create", "window created");
	lds_terminal_window_setup_base(terminal);
	AdwHeaderBar *header = lds_terminal_build_window_shell(terminal);
	lds_terminal_setup_tab_surface(terminal);
	lds_terminal_diag_log("create", "ui initialized");
	lds_terminal_setup_header_controls(terminal, header);

	LdsTerminalTerm *term = NULL;
	if (with_initial_tab) {
		term = lds_terminal_create_initial_tab(terminal, args);
		if (!term) {
			lds_terminal_destroy(terminal);
			return NULL;
		}
	}

	lds_terminal_setup_breakpoints(terminal);
	lds_terminal_update_overview_label(terminal);
	lds_terminal_settings_apply(terminal);
	lds_terminal_tabs_update_alt(terminal);

	gtk_widget_set_visible(terminal->window, TRUE);
	lds_terminal_diag_log("create", "window visible");

	if (term && term->vte)
		lds_terminal_focus_current_term(terminal);

	lds_terminal_diag_log("create", "end");

	return terminal;
}

LdsTerminal *lds_terminal_create(LdsTerminalState *parent, LdsTerminalCommandArgs *args) {
	return lds_terminal_create_internal(parent, args, TRUE);
}

/**
 * lds_terminal_destroy:
 *
 * Destroy a terminal window.
 */
void lds_terminal_destroy(LdsTerminal *terminal) {
	if (!terminal || terminal->destroyed)
		return;

	terminal->destroyed = TRUE;
	terminal->destroy_scheduled = FALSE;
	terminal->close_async_token++;
	if (terminal->focus_recovery_idle_id) {
		g_source_remove(terminal->focus_recovery_idle_id);
		terminal->focus_recovery_idle_id = 0;
	}
	if (terminal->destroy_idle_id) {
		g_source_remove(terminal->destroy_idle_id);
		terminal->destroy_idle_id = 0;
	}

	if (terminal->parent && terminal->parent->windows) {
		g_ptr_array_remove(terminal->parent->windows, terminal);
	}

	lds_terminal_search_reset_cache(terminal);
	if (terminal->search_debounce_id) {
		g_source_remove(terminal->search_debounce_id);
		terminal->search_debounce_id = 0;
	}

	if (terminal->window_key_controller && G_IS_OBJECT(terminal->window_key_controller)) {
		g_signal_handlers_disconnect_by_data(terminal->window_key_controller, terminal);
		terminal->window_key_controller = NULL;
	}

	if (terminal->search_key_controller && G_IS_OBJECT(terminal->search_key_controller)) {
		g_signal_handlers_disconnect_by_data(terminal->search_key_controller, terminal);
		terminal->search_key_controller = NULL;
	}

	if (terminal->window_actions && G_IS_OBJECT(terminal->window_actions)) {
		g_object_unref(terminal->window_actions);
		terminal->window_actions = NULL;
	}
	terminal->menu_actions_cached_from = NULL;
	terminal->action_copy = NULL;
	terminal->action_paste = NULL;
	terminal->action_clear = NULL;
	terminal->action_reset = NULL;
	terminal->action_rename_tab = NULL;
	terminal->action_zoom_in = NULL;
	terminal->action_zoom_out = NULL;
	terminal->action_zoom_reset = NULL;
	terminal->action_split_vertical = NULL;
	terminal->action_close_pane = NULL;
	terminal->action_focus_next_pane = NULL;

	if (terminal->tab_add_button && GTK_IS_WIDGET(terminal->tab_add_button)) {
		g_signal_handlers_disconnect_by_data(terminal->tab_add_button, terminal);
		terminal->tab_add_button = NULL;
	}

	if (terminal->overview_button && GTK_IS_WIDGET(terminal->overview_button)) {
		g_signal_handlers_disconnect_by_data(terminal->overview_button, terminal);
		terminal->overview_button = NULL;
	}

	if (terminal->tab_overview && GTK_IS_WIDGET(terminal->tab_overview))
		g_signal_handlers_disconnect_by_data(terminal->tab_overview, terminal);

	if (terminal->overview_label)
		terminal->overview_label = NULL;

	if (G_IS_OBJECT(adw_style_manager_get_default()))
		g_signal_handlers_disconnect_by_data(adw_style_manager_get_default(), terminal);

	{
		GSettings *settings = lds_terminal_get_settings();
		if (settings && G_IS_OBJECT(settings))
			g_signal_handlers_disconnect_by_data(settings, terminal);
	}

	if (terminal->tab_view && GTK_IS_WIDGET(terminal->tab_view))
		g_signal_handlers_disconnect_by_data(terminal->tab_view, terminal);
	if (terminal->tab_bar && GTK_IS_WIDGET(terminal->tab_bar))
		g_signal_handlers_disconnect_by_data(terminal->tab_bar, terminal);

	if (terminal->window && GTK_IS_WIDGET(terminal->window))
		g_signal_handlers_disconnect_by_data(terminal->window, terminal);

	if (terminal->window && GTK_IS_WIDGET(terminal->window))
		g_object_set_data(G_OBJECT(terminal->window), LDS_TERMINAL_WINDOW_DATA_KEY, NULL);

	if (terminal->window && GTK_IS_WIDGET(terminal->window)) {
		GdkDisplay *display = gtk_widget_get_display(terminal->window);
		if (display) {
			GdkClipboard *clipboard = gdk_display_get_clipboard(display);
			if (clipboard)
				g_signal_handlers_disconnect_by_data(clipboard, terminal);
		}
	}

	if (terminal->search_entry && GTK_IS_WIDGET(terminal->search_entry))
		g_signal_handlers_disconnect_by_data(terminal->search_entry, terminal);
	if (terminal->search_next && GTK_IS_WIDGET(terminal->search_next))
		g_signal_handlers_disconnect_by_data(terminal->search_next, terminal);
	if (terminal->search_prev && GTK_IS_WIDGET(terminal->search_prev))
		g_signal_handlers_disconnect_by_data(terminal->search_prev, terminal);
	if (terminal->search_match_case && GTK_IS_WIDGET(terminal->search_match_case))
		g_signal_handlers_disconnect_by_data(terminal->search_match_case, terminal);
	if (terminal->search_regex && GTK_IS_WIDGET(terminal->search_regex))
		g_signal_handlers_disconnect_by_data(terminal->search_regex, terminal);
	if (terminal->search_whole_word && GTK_IS_WIDGET(terminal->search_whole_word))
		g_signal_handlers_disconnect_by_data(terminal->search_whole_word, terminal);
	if (terminal->search_wrap && GTK_IS_WIDGET(terminal->search_wrap))
		g_signal_handlers_disconnect_by_data(terminal->search_wrap, terminal);
	if (terminal->search_popover && GTK_IS_WIDGET(terminal->search_popover))
		g_signal_handlers_disconnect_by_data(terminal->search_popover, terminal);

	if (terminal->search_popover && GTK_IS_WIDGET(terminal->search_popover)) {
		terminal->search_popover = NULL;
		terminal->search_button = NULL;
		terminal->search_entry = NULL;
		terminal->search_match_case = NULL;
		terminal->search_regex = NULL;
		terminal->search_whole_word = NULL;
		terminal->search_next = NULL;
		terminal->search_prev = NULL;
	}

	if (terminal->current_term_lookup_calls > 0) {
		const gdouble hit_ratio = 100.0 * (gdouble)terminal->current_term_lookup_fast_hits /
								  (gdouble)terminal->current_term_lookup_calls;
		lds_terminal_diag_log(
			"perf",
			"current-term lookup summary calls=%" G_GUINT64_FORMAT " fast-hits=%" G_GUINT64_FORMAT
			" fallback-scans=%" G_GUINT64_FORMAT " hit-ratio=%.1f%%",
			terminal->current_term_lookup_calls, terminal->current_term_lookup_fast_hits,
			terminal->current_term_lookup_fallback_scans, hit_ratio);
	}

	lds_terminal_vte_clear_cached_font_desc();
	g_ptr_array_free(terminal->terms, TRUE);
	g_free(terminal);
}

void lds_terminal_open_search(LdsTerminal *terminal) {
	lds_terminal_search_dialog_show(terminal);
}

static gboolean lds_terminal_on_window_key_press(GtkEventControllerKey *controller, guint keyval,
												 guint keycode, GdkModifierType state,
												 LdsTerminal *terminal) {
	(void)controller;
	(void)keycode;

	if (terminal && terminal->tab_overview && ADW_IS_TAB_OVERVIEW(terminal->tab_overview) &&
		adw_tab_overview_get_open(ADW_TAB_OVERVIEW(terminal->tab_overview)) &&
		lds_terminal_overview_should_consume_enter(keyval, TRUE)) {
		GtkWidget *focus = NULL;
		if (terminal->window && GTK_IS_ROOT(terminal->window))
			focus = gtk_root_get_focus(GTK_ROOT(terminal->window));

		if (focus && GTK_IS_EDITABLE(focus) &&
			gtk_widget_is_ancestor(focus, terminal->tab_overview)) {
			return TRUE;
		}
	}

	if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) &&
		(keyval == GDK_KEY_F || keyval == GDK_KEY_f)) {
		lds_terminal_search_dialog_show(terminal);
		return TRUE;
	}

	return FALSE;
}

static gboolean lds_terminal_on_overview_key_press(GtkEventControllerKey *controller, guint keyval,
												   guint keycode, GdkModifierType state,
												   LdsTerminal *terminal) {
	(void)controller;
	(void)keycode;

	if (!terminal || !terminal->tab_overview || !ADW_IS_TAB_OVERVIEW(terminal->tab_overview))
		return FALSE;

	if (!adw_tab_overview_get_open(ADW_TAB_OVERVIEW(terminal->tab_overview)))
		return FALSE;

	if (lds_terminal_overview_maybe_force_search_on_key(terminal, keyval, state))
		return FALSE;

	if (!lds_terminal_overview_should_consume_enter(keyval, TRUE))
		return FALSE;

	/*
	 * FIXME(upstream): libadwaita Enter handling in overview search can crash
	 * on some stacks; consume Enter while overview is open until upstream fix
	 * is confirmed and this workaround can be removed.
	 */
	lds_terminal_diag_log("overview", "enter consumed while overview open");
	return TRUE;
}

static gboolean lds_terminal_on_tab_close_request(AdwTabView *view, AdwTabPage *page,
												  LdsTerminal *terminal) {
	if (!terminal || !page)
		return FALSE;

	LdsTerminalTerm *term = lds_terminal_find_term_by_page(terminal, page, FALSE);

	if (!term)
		return FALSE;

	if (term->closing) {
		adw_tab_view_close_page_finish(view, page, TRUE);
		lds_terminal_remove_term(terminal, term);
		lds_terminal_schedule_focus_current_term(terminal);
		return TRUE;
	}

	if (lds_terminal_settings_confirm_running_process_enabled() &&
		lds_terminal_vte_term_has_running_jobs(term)) {
		guint jobs = lds_terminal_vte_term_running_job_count(term);
		g_autofree gchar *body = NULL;
		if (jobs > 1) {
			body = g_strdup_printf(
				_("This will terminate %u running foreground jobs in this tab. Continue?"), jobs);
		} else {
			body = g_strdup(
				_("This will terminate a running foreground job in this tab. Continue?"));
		}

		AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Close tab?"), body));
		adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
		adw_alert_dialog_add_response(dlg, "close", _("Close"));
		adw_alert_dialog_set_response_appearance(dlg, "close", ADW_RESPONSE_DESTRUCTIVE);
		adw_alert_dialog_set_default_response(dlg, "cancel");
		adw_alert_dialog_set_close_response(dlg, "cancel");

		LdsTerminalTabCloseConfirmData *data = g_new0(LdsTerminalTabCloseConfirmData, 1);
		g_weak_ref_init(&data->window_ref, G_OBJECT(terminal->window));
		data->close_async_token = terminal->close_async_token;
		data->page = g_object_ref(page);
		g_signal_connect(dlg, "response", G_CALLBACK(lds_terminal_on_tab_close_confirm_response),
						 data);
		adw_dialog_present(ADW_DIALOG(dlg), terminal->window);
		return TRUE;
	}

	term->closing = TRUE;

	if (term->vte)
		g_signal_handlers_disconnect_by_data(term->vte, term);

	if (term->pid > 0) {
		lds_terminal_terminate_child_process(term->pid);
		term->pid = -1;
	}

	adw_tab_view_close_page_finish(view, page, TRUE);
	lds_terminal_remove_term(terminal, term);
	lds_terminal_schedule_focus_current_term(terminal);

	return TRUE;
}

static void lds_terminal_on_tab_close_confirm_response(AdwAlertDialog *dialog, const char *response,
													   gpointer user_data) {
	(void)dialog;
	LdsTerminalTabCloseConfirmData *data = user_data;
	if (!data)
		return;

	GtkWidget *window = g_weak_ref_get(&data->window_ref);
	LdsTerminal *terminal = NULL;
	if (window)
		terminal = g_object_get_data(G_OBJECT(window), LDS_TERMINAL_WINDOW_DATA_KEY);

	if (g_strcmp0(response, "close") == 0 && terminal && !terminal->destroyed &&
		terminal->close_async_token == data->close_async_token && terminal->tab_view &&
		terminal->terms) {
		LdsTerminalTerm *term = lds_terminal_find_term_by_page(terminal, data->page, FALSE);

		if (term && !term->closing) {
			AdwTabView *view = ADW_TAB_VIEW(terminal->tab_view);
			term->closing = TRUE;
			if (term->vte)
				g_signal_handlers_disconnect_by_data(term->vte, term);
			if (term->pid > 0) {
				lds_terminal_terminate_child_process(term->pid);
				term->pid = -1;
			}
			adw_tab_view_close_page_finish(view, term->page, TRUE);
			lds_terminal_remove_term(terminal, term);
			lds_terminal_schedule_focus_current_term(terminal);
		}
	}

	if (window)
		g_object_unref(window);
	g_weak_ref_clear(&data->window_ref);
	if (data->page)
		g_object_unref(data->page);
	g_free(data);
}

static void lds_terminal_on_style_dark_notify(AdwStyleManager *manager, GParamSpec *pspec,
											  LdsTerminal *terminal) {
	(void)pspec;

	if (!terminal)
		return;

	if (lds_terminal_settings_follow_system_theme()) {
		adw_style_manager_set_color_scheme(manager, adw_style_manager_get_dark(manager)
														? ADW_COLOR_SCHEME_PREFER_DARK
														: ADW_COLOR_SCHEME_PREFER_LIGHT);
	}
}

static GSettings *lds_terminal_get_settings(void) {
	return lds_terminal_settings_backend();
}

void lds_terminal_search_notify_contents_changed(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!terminal || !term || term->parent != terminal || terminal->destroyed)
		return;

	if (lds_terminal_get_current_term(terminal) != term)
		return;

	lds_terminal_search_invalidate_snapshot(terminal);
	if (terminal->search_entry) {
		const char *text = gtk_editable_get_text(GTK_EDITABLE(terminal->search_entry));
		if (text && *text)
			lds_terminal_search_schedule_debounce(terminal, lds_terminal_search_debounce_cb);
	}
}

static gboolean lds_terminal_settings_key_is_global(const char *key) {
	if (!key)
		return FALSE;

	return g_strcmp0(key, "tab-position") == 0 || g_strcmp0(key, "hide-menu-bar") == 0 ||
		   g_strcmp0(key, "strict-determinism") == 0 || g_strcmp0(key, "theme-mode") == 0 ||
		   g_strcmp0(key, "follow-system-theme") == 0 || g_strcmp0(key, "confirm-close") == 0 ||
		   g_strcmp0(key, "confirm-running-process") == 0 || g_strcmp0(key, "disable-alt") == 0;
}

static gboolean lds_terminal_settings_key_is_local_vte(const char *key) {
	if (!key)
		return FALSE;

	return g_strcmp0(key, "font-name") == 0 || g_strcmp0(key, "font-color") == 0 ||
		   g_strcmp0(key, "background-color") == 0 || g_strcmp0(key, "sync-prompt-colors") == 0 ||
		   g_strcmp0(key, "scrollback") == 0 || g_strcmp0(key, "cursor-shape") == 0 ||
		   g_strcmp0(key, "cursor-blink") == 0 || g_str_has_prefix(key, "palette-");
}

static void lds_terminal_on_global_settings_changed(GSettings *settings, gchar *key,
													LdsTerminal *terminal) {
	(void)settings;
	if (!terminal || terminal->destroyed || !key)
		return;

	if (lds_terminal_settings_key_is_global(key)) {
		if (!terminal->parent || !terminal->parent->windows)
			return;

		for (guint i = 0; i < terminal->parent->windows->len; i++) {
			LdsTerminal *t = g_ptr_array_index(terminal->parent->windows, i);
			lds_terminal_settings_apply_ui_only(t);
		}
		return;
	}

	if (lds_terminal_settings_key_is_local_vte(key)) {
		lds_terminal_settings_apply_vte_current(terminal);
		if (g_strcmp0(key, "scrollback") == 0)
			lds_terminal_search_invalidate_snapshot(terminal);
	}
}

gboolean lds_terminal_search_count_request_is_duplicate(LdsTerminal *terminal,
														LdsTerminalTerm *term, const char *query,
														guint opt_flags) {
	if (!terminal || !term || !query || *query == '\0')
		return FALSE;

	if (!terminal->search_count_running)
		return FALSE;

	if (terminal->search_count_reschedule && terminal->search_pending_query &&
		terminal->search_pending_term == term && terminal->search_pending_opt_flags == opt_flags &&
		terminal->search_pending_generation == terminal->search_haystack_generation &&
		g_strcmp0(terminal->search_pending_query, query) == 0)
		return TRUE;

	if (terminal->search_count_active_query && terminal->search_count_active_term == term &&
		terminal->search_count_active_opt_flags == opt_flags &&
		terminal->search_count_active_generation == terminal->search_haystack_generation &&
		g_strcmp0(terminal->search_count_active_query, query) == 0)
		return TRUE;

	return FALSE;
}

static void lds_terminal_shortcuts_init(LdsTerminal *terminal) {
	GtkShortcutController *controller = GTK_SHORTCUT_CONTROLLER(gtk_shortcut_controller_new());
	gtk_shortcut_controller_set_scope(controller, GTK_SHORTCUT_SCOPE_LOCAL);
	gtk_widget_add_controller(terminal->window, GTK_EVENT_CONTROLLER(controller));

	gtk_shortcut_controller_add_shortcut(
		controller, gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_g, GDK_CONTROL_MASK),
									 gtk_callback_action_new(lds_terminal_shortcut_search_next,
															 terminal, NULL)));

	gtk_shortcut_controller_add_shortcut(
		controller,
		gtk_shortcut_new(
			gtk_keyval_trigger_new(GDK_KEY_g, GDK_CONTROL_MASK | GDK_SHIFT_MASK),
			gtk_callback_action_new(lds_terminal_shortcut_search_prev, terminal, NULL)));

	gtk_shortcut_controller_add_shortcut(
		controller,
		gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_o, GDK_CONTROL_MASK | GDK_SHIFT_MASK),
						 gtk_callback_action_new(lds_terminal_shortcut_overview, terminal, NULL)));
}

static void lds_terminal_on_new_tab_button_clicked(GtkButton *button, LdsTerminal *terminal) {
	(void)button;
	if (!terminal)
		return;

	lds_terminal_new_tab(terminal, NULL);
}

static AdwTabView *lds_terminal_on_tab_create_window(AdwTabView *view, LdsTerminal *terminal) {
	if (!terminal || !terminal->parent)
		return view;

	LdsTerminal *created =
		lds_terminal_create_internal(terminal->parent, &(LdsTerminalCommandArgs){0}, FALSE);
	if (!created || !created->tab_view) {
		lds_terminal_diag_log("window", "failed to create window for tab detach");
		return view;
	}

	return ADW_TAB_VIEW(created->tab_view);
}

static void lds_terminal_on_clipboard_changed(GdkClipboard *clipboard, LdsTerminal *terminal) {
	(void)clipboard;
	if (!terminal || terminal->destroyed)
		return;

	lds_terminal_menu_invalidate_clipboard_cache(terminal);
	lds_terminal_menu_sync_edit_actions(terminal);
}

static gboolean lds_terminal_shortcut_search_next(GtkWidget *widget, GVariant *args,
												  gpointer user_data) {
	(void)widget;
	(void)args;
	lds_terminal_search_apply(user_data, TRUE);
	return TRUE;
}

static gboolean lds_terminal_shortcut_search_prev(GtkWidget *widget, GVariant *args,
												  gpointer user_data) {
	(void)widget;
	(void)args;
	lds_terminal_search_apply(user_data, FALSE);
	return TRUE;
}

static gboolean lds_terminal_shortcut_overview(GtkWidget *widget, GVariant *args,
											   gpointer user_data) {
	(void)widget;
	(void)args;
	LdsTerminal *terminal = user_data;
	if (!terminal || !terminal->tab_overview)
		return FALSE;

	gboolean open = adw_tab_overview_get_open(ADW_TAB_OVERVIEW(terminal->tab_overview));
	lds_terminal_open_overview(terminal, !open);
	return TRUE;
}

static void lds_terminal_setup_breakpoints(LdsTerminal *terminal) {
	if (!terminal || !terminal->breakpoint_bin)
		return;

	AdwBreakpointCondition *narrow_cond = adw_breakpoint_condition_new_length(
		ADW_BREAKPOINT_CONDITION_MAX_WIDTH, 520, ADW_LENGTH_UNIT_PX);
	AdwBreakpoint *narrow = adw_breakpoint_new(narrow_cond);

	GValue visible_false = G_VALUE_INIT;
	g_value_init(&visible_false, G_TYPE_BOOLEAN);
	g_value_set_boolean(&visible_false, FALSE);

	GValue margin_small = G_VALUE_INIT;
	g_value_init(&margin_small, G_TYPE_INT);
	g_value_set_int(&margin_small, 2);

	if (terminal->title_widget)
		adw_breakpoint_add_setter(narrow, G_OBJECT(terminal->title_widget), "visible",
								  &visible_false);

	if (terminal->menu)
		adw_breakpoint_add_setter(narrow, G_OBJECT(terminal->menu), "margin-end", &margin_small);

	if (terminal->search_button)
		adw_breakpoint_add_setter(narrow, G_OBJECT(terminal->search_button), "margin-start",
								  &margin_small);

	if (terminal->tab_bar) {
		adw_breakpoint_add_setter(narrow, G_OBJECT(terminal->tab_bar), "margin-start",
								  &margin_small);
		adw_breakpoint_add_setter(narrow, G_OBJECT(terminal->tab_bar), "margin-end", &margin_small);
	}

	adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(terminal->breakpoint_bin), narrow);

	AdwBreakpointCondition *wide_cond = adw_breakpoint_condition_new_length(
		ADW_BREAKPOINT_CONDITION_MIN_WIDTH, 521, ADW_LENGTH_UNIT_PX);
	AdwBreakpoint *wide = adw_breakpoint_new(wide_cond);

	GValue visible_true = G_VALUE_INIT;
	g_value_init(&visible_true, G_TYPE_BOOLEAN);
	g_value_set_boolean(&visible_true, TRUE);

	GValue margin_large = G_VALUE_INIT;
	g_value_init(&margin_large, G_TYPE_INT);
	g_value_set_int(&margin_large, 6);

	if (terminal->title_widget)
		adw_breakpoint_add_setter(wide, G_OBJECT(terminal->title_widget), "visible", &visible_true);

	if (terminal->menu)
		adw_breakpoint_add_setter(wide, G_OBJECT(terminal->menu), "margin-end", &margin_large);

	if (terminal->search_button)
		adw_breakpoint_add_setter(wide, G_OBJECT(terminal->search_button), "margin-start",
								  &margin_large);

	if (terminal->tab_bar) {
		adw_breakpoint_add_setter(wide, G_OBJECT(terminal->tab_bar), "margin-start", &margin_large);
		adw_breakpoint_add_setter(wide, G_OBJECT(terminal->tab_bar), "margin-end", &margin_large);
	}

	adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(terminal->breakpoint_bin), wide);

	g_value_unset(&visible_false);
	g_value_unset(&margin_small);
	g_value_unset(&visible_true);
	g_value_unset(&margin_large);
}
