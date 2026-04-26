/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE link hover, click, and context menu handling.
 */

/* Link detection, hover/click handling, and term context menu wiring. */

#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>
#include <vte/vte.h>

#if VTE_CHECK_VERSION(0, 46, 0)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

#include "menu.h"
#include "vte.h"
#include "internal/link_detect.h"
#include "internal/vte_compat.h"
#include "internal/vte_links.h"

/* Link candidate regexes (final acceptance is decided by link_detect). */
static const char k_link_re_url_scheme[] = "(news|telnet|nttp|file|http|ftp|https)://"
										   "[[:alnum:]-]+(\\.[[:alnum:]-]+)*"
										   "(:[[:digit:]]+)?"
										   "(/[[:graph:]]*)?";

static const char k_link_re_email[] =
	"[[:alnum:]_.%+-]+@([[:alnum:]-]+\\.)+(com|org|net|io|dev|app|tech|ai|co|me|info|biz|xyz|"
	"site|online|cloud|store|blog|br|us|uk|de|fr|es|it|pt|nl|ru|jp|kr|cn|ca|au|ch|se|no|fi|dk|"
	"be|pl)(\\b)";

static const char k_link_re_domain[] =
	"([[:alnum:]][[:alnum:]-]*\\.)+(com|org|net|io|dev|app|tech|ai|co|me|info|biz|xyz|site|online|"
	"cloud|store|blog|br|us|uk|de|fr|es|it|pt|nl|ru|jp|kr|cn|ca|au|ch|se|no|fi|dk|be|pl)"
	"(:[[:digit:]]+)?"
	"([/?#][[:graph:]]*)?";

static void lds_terminal_action_term_copy(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data);
static void lds_terminal_action_term_open_link(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data);
static void lds_terminal_action_term_copy_link(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data);
static void lds_terminal_action_term_paste(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_term_clear(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_action_term_reset(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data);
static void lds_terminal_context_menu_show(LdsTerminalTerm *term, GtkWidget *source, double x,
										   double y);
static GtkWidget *lds_terminal_context_menu_get(LdsTerminalTerm *term);
static void lds_terminal_on_context_menu_closed(GtkPopover *popover, LdsTerminalTerm *term);
static gchar *lds_terminal_term_get_context_link_target(LdsTerminalTerm *term);
static void lds_terminal_term_set_action_enabled_if_changed(GAction *action, gboolean enabled);
static void lds_terminal_vte_launch_uri(const gchar *uri);
static void lds_terminal_action_term_run_simple(LdsTerminalTerm *term,
												gboolean (*fn)(LdsTerminalTerm *term));
static gboolean lds_terminal_vte_links_term_alive(LdsTerminalTerm *term);
static gchar *lds_terminal_get_match_at(VteTerminal *vte, double x, double y);
static gchar *lds_terminal_get_link_target_at(VteTerminal *vte, LdsTerminalTerm *term, double x,
											  double y);
static gchar *lds_terminal_get_link_target_at_line_pos(VteTerminal *vte, LdsTerminalTerm *term,
													   double x, double y);
static gint lds_terminal_vte_top_row_guess(VteTerminal *vte, glong char_h);
static gint lds_terminal_link_cache_bucket_for_text(const gchar *text);
static gboolean lds_terminal_url_is_allowed_scheme(const gchar *scheme);
static gchar *lds_terminal_url_sanitize_match(const gchar *raw_match);
static gchar *lds_terminal_url_normalize_from_match(LdsTerminalTerm *term, const gchar *raw_match,
													gboolean force_osc8);

void lds_terminal_vte_links_setup_regex(VteTerminal *vte) {
#if VTE_CHECK_VERSION(0, 46, 0)
	VteRegex *r1 = lds_terminal_vte_regex_new_for_match(
		k_link_re_url_scheme, -1,
		PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_UCP | PCRE2_MULTILINE | PCRE2_CASELESS, NULL);
	VteRegex *r2 = lds_terminal_vte_regex_new_for_match(
		k_link_re_email, -1,
		PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_UCP | PCRE2_MULTILINE | PCRE2_CASELESS, NULL);
	VteRegex *r3 = lds_terminal_vte_regex_new_for_match(
		k_link_re_domain, -1,
		PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_UCP | PCRE2_MULTILINE | PCRE2_CASELESS, NULL);

	(void)lds_terminal_vte_terminal_match_add_regex(vte, r1, 0);
	(void)lds_terminal_vte_terminal_match_add_regex(vte, r2, 0);
	(void)lds_terminal_vte_terminal_match_add_regex(vte, r3, 0);

	lds_terminal_vte_regex_unref(r1);
	lds_terminal_vte_regex_unref(r2);
	lds_terminal_vte_regex_unref(r3);
#else
	(void)vte;
#endif
}

void lds_terminal_vte_links_cleanup_term(LdsTerminalTerm *term, gboolean ui_teardown) {
	if (!term)
		return;

	if (!ui_teardown && term->context_menu && GTK_IS_WIDGET(term->context_menu)) {
		if (gtk_widget_get_parent(term->context_menu))
			gtk_widget_unparent(term->context_menu);
	}
	term->context_menu = NULL;
	term->context_source_vte = NULL;
	g_clear_pointer(&term->context_link_target, g_free);

	if (term->context_actions) {
		if (!ui_teardown && term->box && GTK_IS_WIDGET(term->box))
			gtk_widget_insert_action_group(term->box, "term", NULL);
		g_object_unref(term->context_actions);
		term->context_actions = NULL;
	}
}

void lds_terminal_vte_links_on_click_primary(GtkGestureClick *gesture, int n_press, double x,
											 double y, LdsTerminalTerm *term) {
	(void)n_press;
	if (!lds_terminal_vte_links_term_alive(term))
		return;

	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GdkModifierType state =
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if ((state & GDK_CONTROL_MASK) == 0)
		return;

	if (!widget || !VTE_IS_TERMINAL(widget))
		return;

	g_autofree gchar *uri = lds_terminal_get_link_target_at(VTE_TERMINAL(widget), term, x, y);
	if (!uri)
		return;

	lds_terminal_vte_launch_uri(uri);
}

void lds_terminal_vte_links_on_click_secondary(GtkGestureClick *gesture, int n_press, double x,
											   double y, LdsTerminalTerm *term) {
	if (!lds_terminal_vte_links_term_alive(term))
		return;

	if (n_press != 1)
		return;

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	lds_terminal_context_menu_show(term, widget, x, y);
}

void lds_terminal_vte_links_on_motion(GtkEventControllerMotion *motion, double x, double y,
									  LdsTerminalTerm *term) {
	if (!lds_terminal_vte_links_term_alive(term))
		return;

	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
	if (!widget || !VTE_IS_TERMINAL(widget))
		return;

	g_autofree gchar *uri = lds_terminal_get_link_target_at(VTE_TERMINAL(widget), term, x, y);
	gtk_widget_set_cursor_from_name(widget, uri ? "pointer" : NULL);
}

void lds_terminal_vte_links_on_leave(GtkEventControllerMotion *motion, LdsTerminalTerm *term) {
	(void)term;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(motion));
	if (!widget)
		return;
	gtk_widget_set_cursor_from_name(widget, NULL);
}

void lds_terminal_vte_links_on_contents_changed(VteTerminal *vte, LdsTerminalTerm *term) {
	if (!lds_terminal_vte_links_term_alive(term))
		return;

	if (term->link_cache && VTE_IS_TERMINAL(vte)) {
		glong rows = lds_terminal_vte_terminal_get_row_count(vte);
		glong char_h = lds_terminal_vte_terminal_get_char_height(vte);
		if (rows > 0 && char_h > 0) {
			gint top = lds_terminal_vte_top_row_guess(vte, char_h);
			lds_link_line_cache_invalidate_range(term->link_cache, top, top + (gint)rows);
		}
	}

	lds_terminal_search_notify_contents_changed(term->parent, term);
}

void lds_terminal_vte_links_on_text_scrolled(VteTerminal *vte, gint delta, LdsTerminalTerm *term) {
	(void)delta;
	if (!lds_terminal_vte_links_term_alive(term) || !term->link_cache)
		return;

	glong rows = lds_terminal_vte_terminal_get_row_count(vte);
	glong char_h = lds_terminal_vte_terminal_get_char_height(vte);
	if (rows <= 0 || char_h <= 0)
		return;

	gint top = lds_terminal_vte_top_row_guess(vte, char_h);
	lds_link_line_cache_invalidate_range(term->link_cache, top, top + (gint)rows);
}

void lds_terminal_vte_sync_context_actions(LdsTerminalTerm *term) {
	if (!term || !term->context_actions)
		return;

	GAction *copy = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "copy");
	GAction *paste = g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "paste");
	GAction *open_link =
		g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "open-link");
	GAction *copy_link =
		g_action_map_lookup_action(G_ACTION_MAP(term->context_actions), "copy-link");
	gboolean paste_enabled = FALSE;
	if (term->parent)
		paste_enabled = lds_terminal_menu_clipboard_has_text_cached(term->parent, term);
	else
		paste_enabled = lds_terminal_vte_active_clipboard_has_text(term);
	g_autofree gchar *link_target = lds_terminal_term_get_context_link_target(term);
	const gboolean has_link_target = link_target && *link_target;

	lds_terminal_term_set_action_enabled_if_changed(copy,
													 lds_terminal_vte_active_has_selection(term));
	lds_terminal_term_set_action_enabled_if_changed(paste, paste_enabled);
	lds_terminal_term_set_action_enabled_if_changed(open_link, has_link_target);
	lds_terminal_term_set_action_enabled_if_changed(copy_link, has_link_target);
}

static gboolean lds_terminal_vte_links_term_alive(LdsTerminalTerm *term) {
	if (!term || !term->parent || term->closing || term->parent->destroyed || !term->parent->terms)
		return FALSE;

	if (term->index >= 0 && (guint)term->index < term->parent->terms->len &&
		g_ptr_array_index(term->parent->terms, term->index) == term)
		return TRUE;

	for (guint i = 0; i < term->parent->terms->len; i++) {
		if (g_ptr_array_index(term->parent->terms, i) == term)
			return TRUE;
	}

	return FALSE;
}

static void lds_terminal_context_menu_show(LdsTerminalTerm *term, GtkWidget *source, double x,
										   double y) {
	if (!lds_terminal_vte_links_term_alive(term) || !term->box)
		return;

	GtkWidget *popover = lds_terminal_context_menu_get(term);
	if (!popover)
		return;

	GtkWidget *anchor = term->box;
	double anchor_x = x;
	double anchor_y = y;
	if (source && GTK_IS_WIDGET(source) && anchor && GTK_IS_WIDGET(anchor) && source != anchor) {
		graphene_point_t src_point = {.x = (float)x, .y = (float)y};
		graphene_point_t dst_point = {0};
		if (gtk_widget_compute_point(source, anchor, &src_point, &dst_point)) {
			anchor_x = dst_point.x;
			anchor_y = dst_point.y;
		}
	}

	if (gtk_widget_get_visible(popover))
		gtk_popover_popdown(GTK_POPOVER(popover));

	term->context_source_vte = (source && VTE_IS_TERMINAL(source)) ? source : NULL;
	term->context_x = x;
	term->context_y = y;
	g_clear_pointer(&term->context_link_target, g_free);
	if (term->context_source_vte) {
		term->context_link_target = lds_terminal_get_link_target_at(
			VTE_TERMINAL(term->context_source_vte), term, term->context_x, term->context_y);
	}
	g_object_set_data(G_OBJECT(popover), "lds-context-vte", term->context_source_vte);

	GdkRectangle rect = {(int)anchor_x, (int)anchor_y, 1, 1};
	lds_terminal_vte_sync_context_actions(term);
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
	gtk_popover_popup(GTK_POPOVER(popover));
}

static GtkWidget *lds_terminal_context_menu_get(LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return NULL;

	if (term->context_menu && GTK_IS_WIDGET(term->context_menu))
		return term->context_menu;

	GMenuModel *model = lds_terminal_menu_build_context_model();
	GtkWidget *popover = gtk_popover_menu_new_from_model(model);
	g_object_unref(model);
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	gtk_widget_set_parent(popover, term->box);
	g_signal_connect(popover, "closed", G_CALLBACK(lds_terminal_on_context_menu_closed), term);
	term->context_menu = popover;

	if (!term->context_actions) {
		GSimpleActionGroup *group = g_simple_action_group_new();
		const GActionEntry entries[] = {
			{"open-link", lds_terminal_action_term_open_link, NULL, NULL, NULL, {0}},
			{"copy-link", lds_terminal_action_term_copy_link, NULL, NULL, NULL, {0}},
			{"copy", lds_terminal_action_term_copy, NULL, NULL, NULL, {0}},
			{"paste", lds_terminal_action_term_paste, NULL, NULL, NULL, {0}},
			{"clear", lds_terminal_action_term_clear, NULL, NULL, NULL, {0}},
			{"reset", lds_terminal_action_term_reset, NULL, NULL, NULL, {0}},
		};

		g_action_map_add_action_entries(G_ACTION_MAP(group), entries, G_N_ELEMENTS(entries), term);
		gtk_widget_insert_action_group(term->box, "term", G_ACTION_GROUP(group));
		term->context_actions = group;
	}

	return popover;
}

static void lds_terminal_on_context_menu_closed(GtkPopover *popover, LdsTerminalTerm *term) {
	(void)popover;
	if (!term)
		return;
	term->context_source_vte = NULL;
	term->context_x = 0.0;
	term->context_y = 0.0;
	g_clear_pointer(&term->context_link_target, g_free);
	if (term->context_menu)
		g_object_set_data(G_OBJECT(term->context_menu), "lds-context-vte", NULL);
	lds_terminal_schedule_focus_current_term(term->parent);
}

static void lds_terminal_action_term_copy(GSimpleAction *action, GVariant *parameter,
										  gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_action_term_run_simple(user_data, lds_terminal_vte_copy);
}

static gchar *lds_terminal_term_get_context_link_target(LdsTerminalTerm *term) {
	if (!term)
		return NULL;
	if (term->context_link_target && *term->context_link_target)
		return g_strdup(term->context_link_target);
	if (!term->context_source_vte || !VTE_IS_TERMINAL(term->context_source_vte))
		return NULL;

	g_clear_pointer(&term->context_link_target, g_free);
	term->context_link_target = lds_terminal_get_link_target_at(
		VTE_TERMINAL(term->context_source_vte), term, term->context_x, term->context_y);
	return term->context_link_target ? g_strdup(term->context_link_target) : NULL;
}

static void lds_terminal_action_term_open_link(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data) {
	(void)action;
	(void)parameter;
	LdsTerminalTerm *term = user_data;
	if (!term)
		return;

	g_autofree gchar *uri = lds_terminal_term_get_context_link_target(term);
	if (!uri || !*uri)
		return;

	lds_terminal_vte_launch_uri(uri);
}

static void lds_terminal_action_term_copy_link(GSimpleAction *action, GVariant *parameter,
											   gpointer user_data) {
	(void)action;
	(void)parameter;
	LdsTerminalTerm *term = user_data;
	if (!term || !term->context_source_vte)
		return;

	g_autofree gchar *uri = lds_terminal_term_get_context_link_target(term);
	if (!uri || !*uri)
		return;

	GdkDisplay *display = gtk_widget_get_display(term->context_source_vte);
	if (!display)
		return;
	GdkClipboard *clipboard = gdk_display_get_clipboard(display);
	if (!clipboard)
		return;
	gdk_clipboard_set_text(clipboard, uri);
}

static void lds_terminal_action_term_paste(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_action_term_run_simple(user_data, lds_terminal_vte_paste);
}

static void lds_terminal_action_term_clear(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_action_term_run_simple(user_data, lds_terminal_vte_clear);
}

static void lds_terminal_action_term_reset(GSimpleAction *action, GVariant *parameter,
										   gpointer user_data) {
	(void)action;
	(void)parameter;
	lds_terminal_action_term_run_simple(user_data, lds_terminal_vte_reset);
}

static void lds_terminal_term_set_action_enabled_if_changed(GAction *action, gboolean enabled) {
	if (!action || !G_IS_SIMPLE_ACTION(action))
		return;

	if (g_action_get_enabled(action) == enabled)
		return;

	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

static void lds_terminal_vte_launch_uri(const gchar *uri) {
	if (!uri || !*uri)
		return;

	g_autoptr(GError) error = NULL;
	if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
		g_warning("Failed to open URI '%s': %s", uri, error ? error->message : "unknown error");
	}
}

static void lds_terminal_action_term_run_simple(LdsTerminalTerm *term,
												gboolean (*fn)(LdsTerminalTerm *term)) {
	if (!term || !fn)
		return;

	(void)fn(term);
}

static gchar *lds_terminal_get_match_at(VteTerminal *vte, double x, double y) {
	gint tag;
	return lds_terminal_vte_terminal_match_check_at(vte, x, y, &tag);
}

static gchar *lds_terminal_get_link_target_at(VteTerminal *vte, LdsTerminalTerm *term, double x,
											  double y) {
	if (!vte)
		return NULL;

	g_autofree gchar *osc8 = lds_terminal_vte_terminal_hyperlink_check_at(vte, x, y);
	if (osc8) {
		g_autofree gchar *normalized = lds_terminal_url_normalize_from_match(term, osc8, TRUE);
		if (normalized)
			return g_steal_pointer(&normalized);
	}

	g_autofree gchar *line_target = lds_terminal_get_link_target_at_line_pos(vte, term, x, y);
	if (line_target)
		return g_steal_pointer(&line_target);

	g_autofree gchar *match = lds_terminal_get_match_at(vte, x, y);
	if (!match)
		return NULL;

	return lds_terminal_url_normalize_from_match(term, match, FALSE);
}

static gchar *lds_terminal_get_link_target_at_line_pos(VteTerminal *vte, LdsTerminalTerm *term,
													   double x, double y) {
	if (!vte || !term || !term->link_cache)
		return NULL;

	glong char_w = lds_terminal_vte_terminal_get_char_width(vte);
	glong char_h = lds_terminal_vte_terminal_get_char_height(vte);
	glong cols = lds_terminal_vte_terminal_get_column_count(vte);
	glong rows = lds_terminal_vte_terminal_get_row_count(vte);
	if (char_w <= 0 || char_h <= 0 || cols <= 0 || rows <= 0)
		return NULL;

	gint col = (gint)(x / (double)char_w);
	gint row_in_view = (gint)(y / (double)char_h);
	if (col < 0 || row_in_view < 0 || col >= (gint)cols || row_in_view >= (gint)rows)
		return NULL;

	gint top_row = lds_terminal_vte_top_row_guess(vte, char_h);
	gint row_candidates[2] = {top_row + row_in_view, row_in_view};
	for (guint ri = 0; ri < G_N_ELEMENTS(row_candidates); ri++) {
		gint row = row_candidates[ri];
		if (row < 0)
			continue;

		gsize raw_len = 0;
		g_autofree gchar *raw = lds_terminal_vte_terminal_get_text_range_format(
			vte, VTE_FORMAT_TEXT, row, 0, row, cols - 1, &raw_len);
		if (!raw || raw_len == 0)
			continue;
		g_strchomp(raw);
		if (!*raw)
			continue;

		const GPtrArray *spans = NULL;
		g_autofree gchar *osc8_uri = lds_terminal_vte_terminal_hyperlink_check_at(vte, x, y);
		if (osc8_uri && *osc8_uri) {
			LdsOsc8Span osc8_span = {
				.start_col = (guint)col,
				.end_col = (guint)col + 1u,
				.uri = osc8_uri,
			};
			spans = lds_link_detect_line(raw, &osc8_span, 1);
		} else {
			gboolean recomputed = FALSE;
			spans = lds_link_line_cache_get_or_detect(
				term->link_cache, row, term->link_cache_generation, raw, NULL, 0, &recomputed);
			(void)recomputed;
		}
		if (!spans || spans->len == 0) {
			if (osc8_uri)
				g_ptr_array_unref((GPtrArray *)spans);
			continue;
		}

		for (guint i = 0; i < spans->len; i++) {
			LdsLinkSpan *span = g_ptr_array_index((GPtrArray *)spans, i);
			if (!span || !span->target)
				continue;
			if (col >= (gint)span->start_col && col < (gint)span->end_col) {
				gchar *target = g_strdup(span->target);
				if (osc8_uri)
					g_ptr_array_unref((GPtrArray *)spans);
				return target;
			}
		}

		if (osc8_uri)
			g_ptr_array_unref((GPtrArray *)spans);
	}

	return NULL;
}

static gint lds_terminal_vte_top_row_guess(VteTerminal *vte, glong char_h) {
	gint top_row = 0;
	if (!vte || char_h <= 0)
		return 0;

	if (GTK_IS_SCROLLABLE(vte)) {
		GtkAdjustment *vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte));
		if (vadj) {
			double value = gtk_adjustment_get_value(vadj);
			if (lds_terminal_vte_terminal_get_scroll_unit_is_pixels(vte))
				top_row = (gint)(value / (double)char_h);
			else
				top_row = (gint)value;
		}
	}
	return top_row;
}

static gboolean lds_terminal_url_is_allowed_scheme(const gchar *scheme) {
	if (!scheme || !*scheme)
		return FALSE;

	return g_ascii_strcasecmp(scheme, "http") == 0 || g_ascii_strcasecmp(scheme, "https") == 0 ||
		   g_ascii_strcasecmp(scheme, "ftp") == 0 || g_ascii_strcasecmp(scheme, "file") == 0 ||
		   g_ascii_strcasecmp(scheme, "telnet") == 0 || g_ascii_strcasecmp(scheme, "news") == 0 ||
		   g_ascii_strcasecmp(scheme, "nntp") == 0 || g_ascii_strcasecmp(scheme, "nttp") == 0 ||
		   g_ascii_strcasecmp(scheme, "mailto") == 0;
}

static gchar *lds_terminal_url_sanitize_match(const gchar *raw_match) {
	if (!raw_match || !*raw_match)
		return NULL;

	gchar *s = g_strdup(raw_match);
	g_strstrip(s);

	while (*s && strchr("([<{\"'`", *s) != NULL)
		memmove(s, s + 1, strlen(s));

	size_t len = strlen(s);
	while (len > 0) {
		char c = s[len - 1];
		if (strchr(")]>}\"'`.,;:!?", c) == NULL)
			break;
		s[--len] = '\0';
	}

	if (!*s) {
		g_free(s);
		return NULL;
	}

	for (const char *p = s; *p; p++) {
		if (g_ascii_iscntrl(*p) || g_ascii_isspace(*p)) {
			g_free(s);
			return NULL;
		}
	}

	return s;
}

static gint lds_terminal_link_cache_bucket_for_text(const gchar *text) {
	if (!text || !*text)
		return 0;
	return (gint)(g_str_hash(text) % 1024u);
}

static gchar *lds_terminal_url_normalize_from_match(LdsTerminalTerm *term, const gchar *raw_match,
													gboolean force_osc8) {
	g_autofree gchar *sanitized = lds_terminal_url_sanitize_match(raw_match);
	if (!sanitized)
		return NULL;

	if (force_osc8) {
		const gchar *scheme = g_uri_peek_scheme(sanitized);
		if (!scheme || !lds_terminal_url_is_allowed_scheme(scheme))
			return NULL;
		g_autoptr(GError) parse_error = NULL;
		GUri *parsed = g_uri_parse(sanitized, G_URI_FLAGS_NONE, &parse_error);
		if (!parsed)
			return NULL;
		g_uri_unref(parsed);
		return g_strdup(sanitized);
	}

	const GPtrArray *spans = NULL;
	if (term && term->link_cache) {
		gboolean recomputed = FALSE;
		spans = lds_link_line_cache_get_or_detect(
			term->link_cache, lds_terminal_link_cache_bucket_for_text(sanitized),
			term->link_cache_generation, sanitized, NULL, 0, &recomputed);
		(void)recomputed;
	} else {
		spans = lds_link_detect_line(sanitized, NULL, 0);
	}

	if (!spans || spans->len == 0) {
		if ((!term || !term->link_cache) && spans)
			g_ptr_array_unref((GPtrArray *)spans);
		return NULL;
	}

	LdsLinkSpan *best = g_ptr_array_index(spans, 0);
	if (!best || !best->target || !*best->target) {
		if (!term || !term->link_cache)
			g_ptr_array_unref((GPtrArray *)spans);
		return NULL;
	}

	if (!term || !term->link_cache)
		g_ptr_array_unref((GPtrArray *)spans);

	return g_strdup(best->target);
}
