/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Search bar UI and terminal search interaction.
 */

#include <glib/gi18n.h>

#include "internal/search_ui.h"

#include <vte/vte.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "settings.h"
#include "internal/search_engine.h"
#include "internal/vte_compat.h"

static void lds_terminal_search_store_history(GSettings *settings, const char *text);
static void lds_terminal_on_search_entry_changed(GtkEditable *editable, LdsTerminal *terminal);
static void lds_terminal_on_search_entry_activate(GtkEntry *entry, LdsTerminal *terminal);
static void lds_terminal_on_search_next(GtkButton *button, LdsTerminal *terminal);
static void lds_terminal_on_search_prev(GtkButton *button, LdsTerminal *terminal);
static void lds_terminal_on_search_close(GtkButton *button, LdsTerminal *terminal);
static void lds_terminal_on_search_popover_closed(GtkPopover *popover, LdsTerminal *terminal);
static gboolean lds_terminal_on_search_popover_key(GtkEventControllerKey *controller, guint keyval,
												   guint keycode, GdkModifierType state,
												   LdsTerminal *terminal);
static void lds_terminal_on_search_toggle(GtkToggleButton *button, LdsTerminal *terminal);

static void lds_terminal_search_store_history(GSettings *settings, const char *text) {
	if (!settings || !text || !*text)
		return;

	g_auto(GStrv) items = g_settings_get_strv(settings, "search-history");
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);

	g_ptr_array_add(arr, g_strdup(text));

	if (items) {
		for (guint i = 0; items[i] != NULL; i++) {
			if (g_strcmp0(items[i], text) == 0)
				continue;

			g_ptr_array_add(arr, g_strdup(items[i]));
			if (arr->len >= 10)
				break;
		}
	}

	g_ptr_array_add(arr, NULL);
	g_settings_set_strv(settings, "search-history", (const gchar *const *)arr->pdata);
	g_ptr_array_free(arr, TRUE);
}

void lds_terminal_search_dialog_init(LdsTerminal *terminal) {
	GtkWidget *button;
	GtkWidget *popover;
	GtkWidget *content;
	GtkWidget *row;
	GtkWidget *entry;
	GtkWidget *next_btn;
	GtkWidget *prev_btn;
	GtkWidget *count_label;
	GtkWidget *close_btn;
	GtkWidget *toggles;
	GtkWidget *match_case;
	GtkWidget *regex;
	GtkWidget *whole_word;
	GtkWidget *wrap;
	GSettings *settings;

	if (!terminal)
		return;

	button = gtk_menu_button_new();
	gtk_widget_add_css_class(button, "flat");
	gtk_widget_set_tooltip_text(button, _("Search"));
	gtk_menu_button_set_child(GTK_MENU_BUTTON(button),
							  gtk_image_new_from_icon_name("system-search-symbolic"));

	popover = gtk_popover_new();
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
	gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
	gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
	g_signal_connect(popover, "closed", G_CALLBACK(lds_terminal_on_search_popover_closed),
					 terminal);
	{
		GtkEventController *key = gtk_event_controller_key_new();
		terminal->search_key_controller = key;
		g_signal_connect(key, "key-pressed", G_CALLBACK(lds_terminal_on_search_popover_key),
						 terminal);
		gtk_widget_add_controller(popover, key);
	}

	content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(content, 12);
	gtk_widget_set_margin_bottom(content, 12);
	gtk_widget_set_margin_start(content, 12);
	gtk_widget_set_margin_end(content, 12);
	gtk_widget_set_size_request(content, 400, -1);

	row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	entry = gtk_search_entry_new();
	gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(entry), _("Search in output..."));
	gtk_widget_set_hexpand(entry, TRUE);

	prev_btn = gtk_button_new_from_icon_name("go-previous-symbolic");
	gtk_widget_add_css_class(prev_btn, "flat");
	gtk_widget_set_tooltip_text(prev_btn, _("Previous"));

	next_btn = gtk_button_new_from_icon_name("go-next-symbolic");
	gtk_widget_add_css_class(next_btn, "flat");
	gtk_widget_set_tooltip_text(next_btn, _("Next"));

	count_label = gtk_label_new(_("0 matches"));
	gtk_widget_set_halign(count_label, GTK_ALIGN_START);
	gtk_widget_set_valign(count_label, GTK_ALIGN_CENTER);

	close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
	gtk_widget_add_css_class(close_btn, "flat");
	gtk_widget_set_tooltip_text(close_btn, _("Close"));

	gtk_box_append(GTK_BOX(row), entry);
	gtk_box_append(GTK_BOX(row), prev_btn);
	gtk_box_append(GTK_BOX(row), next_btn);
	gtk_box_append(GTK_BOX(row), count_label);
	gtk_box_append(GTK_BOX(row), close_btn);

	toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	match_case = gtk_toggle_button_new_with_label(_("Match Case"));
	regex = gtk_toggle_button_new_with_label(_("Regex"));
	whole_word = gtk_toggle_button_new_with_label(_("Whole Word"));
	wrap = gtk_toggle_button_new_with_label(_("Wrap"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap), TRUE);

	gtk_widget_set_tooltip_text(match_case, _("Match case (A ≠ a)"));
	gtk_widget_set_tooltip_text(regex, _("Use regular expressions"));
	gtk_widget_set_tooltip_text(whole_word, _("Match whole words only"));
	gtk_widget_set_tooltip_text(wrap, _("Wrap around"));

	gtk_box_append(GTK_BOX(content), row);
	gtk_box_append(GTK_BOX(content), toggles);

	gtk_box_append(GTK_BOX(toggles), match_case);
	gtk_box_append(GTK_BOX(toggles), regex);
	gtk_box_append(GTK_BOX(toggles), whole_word);
	gtk_box_append(GTK_BOX(toggles), wrap);

	gtk_popover_set_child(GTK_POPOVER(popover), content);

	g_signal_connect(entry, "changed", G_CALLBACK(lds_terminal_on_search_entry_changed), terminal);
	g_signal_connect(entry, "activate", G_CALLBACK(lds_terminal_on_search_entry_activate),
					 terminal);
	g_signal_connect(next_btn, "clicked", G_CALLBACK(lds_terminal_on_search_next), terminal);
	g_signal_connect(prev_btn, "clicked", G_CALLBACK(lds_terminal_on_search_prev), terminal);
	g_signal_connect(close_btn, "clicked", G_CALLBACK(lds_terminal_on_search_close), terminal);
	g_signal_connect(match_case, "toggled", G_CALLBACK(lds_terminal_on_search_toggle), terminal);
	g_signal_connect(regex, "toggled", G_CALLBACK(lds_terminal_on_search_toggle), terminal);
	g_signal_connect(whole_word, "toggled", G_CALLBACK(lds_terminal_on_search_toggle), terminal);
	g_signal_connect(wrap, "toggled", G_CALLBACK(lds_terminal_on_search_toggle), terminal);

	settings = lds_terminal_settings_backend();
	if (settings) {
		g_settings_bind(settings, "search-match-case", match_case, "active",
						G_SETTINGS_BIND_DEFAULT);
		g_settings_bind(settings, "search-regex", regex, "active", G_SETTINGS_BIND_DEFAULT);
		g_settings_bind(settings, "search-whole-word", whole_word, "active",
						G_SETTINGS_BIND_DEFAULT);
		g_settings_bind(settings, "search-wrap", wrap, "active", G_SETTINGS_BIND_DEFAULT);
	}

	terminal->search_popover = popover;
	terminal->search_button = button;
	terminal->search_entry = entry;
	terminal->search_match_case = match_case;
	terminal->search_regex = regex;
	terminal->search_whole_word = whole_word;
	terminal->search_wrap = wrap;
	terminal->search_next = next_btn;
	terminal->search_prev = prev_btn;
	terminal->search_count_label = count_label;
}

void lds_terminal_search_dialog_show(LdsTerminal *terminal) {
	if (!terminal || !terminal->search_popover || !terminal->search_entry)
		return;

	gtk_popover_popup(GTK_POPOVER(terminal->search_popover));
	gtk_widget_grab_focus(terminal->search_entry);
	gtk_editable_select_region(GTK_EDITABLE(terminal->search_entry), 0, -1);
}

void lds_terminal_search_update_regex(LdsTerminal *terminal) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	const gchar *text;
	GRegex *count_regex = NULL;
	VteRegex *vte_regex;
	guint32 flags = 0;

	if (!terminal || !term || !terminal->search_entry)
		return;

	text = gtk_editable_get_text(GTK_EDITABLE(terminal->search_entry));
	if (!text || *text == '\0') {
		lds_terminal_search_reset_cache(terminal);
		lds_terminal_vte_terminal_search_set_regex(VTE_TERMINAL(term->vte), NULL, 0);

		if (terminal->search_next)
			gtk_widget_set_sensitive(terminal->search_next, FALSE);

		if (terminal->search_prev)
			gtk_widget_set_sensitive(terminal->search_prev, FALSE);

		if (terminal->search_count_label)
			gtk_label_set_text(GTK_LABEL(terminal->search_count_label), _("0 matches"));

		return;
	}

	gboolean match_case = TRUE;
	gboolean use_regex = FALSE;
	gboolean whole_word = FALSE;
	gboolean wrap = TRUE;
	gboolean valid_regex = TRUE;
	guint opt_flags = 0;
	lds_terminal_search_read_options(terminal, &match_case, &use_regex, &whole_word, &wrap,
									 &opt_flags);

	gboolean same_query_and_flags = terminal->search_last_text &&
									g_strcmp0(terminal->search_last_text, text) == 0 &&
									terminal->search_last_flags == opt_flags;
	if (same_query_and_flags) {
		if (terminal->search_total_cache_valid)
			return;

		if (!terminal->search_last_valid_regex) {
			lds_terminal_search_cancel_count_job(terminal, TRUE);
			lds_terminal_search_update_count_label(terminal->search_count_label, FALSE, 0, FALSE,
												   FALSE);
			return;
		}

		if (terminal->search_count_running &&
			lds_terminal_search_count_request_is_duplicate(terminal, term, text, opt_flags)) {
			lds_terminal_search_update_count_label(terminal->search_count_label, TRUE, 0, FALSE,
												   TRUE);
			return;
		}

		lds_terminal_search_schedule_count(terminal, term, text, match_case, use_regex, whole_word,
										   opt_flags);
		return;
	}

	g_free(terminal->search_last_text);
	terminal->search_last_text = g_strdup(text);
	terminal->search_last_flags = opt_flags;
	terminal->search_total_cache_valid = FALSE;
	terminal->search_last_approximate = FALSE;
	terminal->search_last_valid_regex = TRUE;

	if (!match_case)
		flags |= PCRE2_CASELESS;

	flags |= PCRE2_MULTILINE;

	g_autofree gchar *pattern = use_regex ? g_strdup(text) : g_regex_escape_string(text, -1);
	if (whole_word) {
		g_autofree gchar *wrapped = g_strdup_printf("\\b%s\\b", pattern);
		pattern = g_steal_pointer(&wrapped);
	}

	vte_regex = lds_terminal_vte_regex_new_for_search(pattern, -1, flags, NULL);
	if (!vte_regex) {
		lds_terminal_vte_terminal_search_set_regex(VTE_TERMINAL(term->vte), NULL, 0);
		lds_terminal_search_cancel_count_job(terminal, TRUE);
		if (terminal->search_next)
			gtk_widget_set_sensitive(terminal->search_next, FALSE);
		if (terminal->search_prev)
			gtk_widget_set_sensitive(terminal->search_prev, FALSE);
		terminal->search_last_total_matches = 0;
		terminal->search_last_valid_regex = FALSE;
		terminal->search_last_approximate = FALSE;
		terminal->search_total_cache_valid = TRUE;
		lds_terminal_search_update_count_label(terminal->search_count_label, FALSE, 0, FALSE,
											   FALSE);
		return;
	}

	lds_terminal_vte_terminal_search_set_regex(VTE_TERMINAL(term->vte), vte_regex, 0);
	lds_terminal_vte_terminal_search_set_wrap_around(VTE_TERMINAL(term->vte), wrap);
	lds_terminal_vte_regex_unref(vte_regex);

	if (terminal->search_next)
		gtk_widget_set_sensitive(terminal->search_next, TRUE);

	if (terminal->search_prev)
		gtk_widget_set_sensitive(terminal->search_prev, TRUE);

	count_regex = lds_terminal_search_get_cached_regex(terminal, text, match_case, use_regex,
													   whole_word, opt_flags, &valid_regex);
	if (!valid_regex || !count_regex) {
		lds_terminal_search_cancel_count_job(terminal, TRUE);
		terminal->search_last_total_matches = 0;
		terminal->search_last_valid_regex = FALSE;
		terminal->search_last_approximate = FALSE;
		terminal->search_total_cache_valid = TRUE;
		lds_terminal_search_update_count_label(terminal->search_count_label, FALSE, 0, FALSE,
											   FALSE);
		return;
	}

	g_regex_unref(count_regex);
	lds_terminal_search_schedule_count(terminal, term, text, match_case, use_regex, whole_word,
									   opt_flags);
}

gboolean lds_terminal_search_apply(LdsTerminal *terminal, gboolean forward) {
	LdsTerminalTerm *term = lds_terminal_get_current_term(terminal);
	GSettings *settings;
	const char *text;
	gboolean found = FALSE;

	if (!terminal || !term || !terminal->search_entry)
		return FALSE;

	settings = lds_terminal_settings_backend();
	text = gtk_editable_get_text(GTK_EDITABLE(terminal->search_entry));
	if (settings && text && *text)
		lds_terminal_search_store_history(settings, text);

	lds_terminal_search_update_regex(terminal);

	if (forward)
		found = lds_terminal_vte_terminal_search_find_next(VTE_TERMINAL(term->vte));
	else
		found = lds_terminal_vte_terminal_search_find_previous(VTE_TERMINAL(term->vte));

	lds_terminal_search_update_count_label(
		terminal->search_count_label, terminal->search_last_valid_regex,
		terminal->search_last_total_matches, terminal->search_last_approximate,
		!terminal->search_total_cache_valid);

	return found;
}

void lds_terminal_search_update_count_label(GtkWidget *label, gboolean valid_regex,
											 guint total_matches, gboolean approximate,
											 gboolean pending) {
	if (!label)
		return;

	if (pending) {
		gtk_label_set_text(GTK_LABEL(label), _("Counting..."));
		return;
	}

	if (!valid_regex) {
		gtk_label_set_text(GTK_LABEL(label), _("Invalid regex"));
		return;
	}

	g_autofree gchar *text = NULL;
	if (approximate)
		text = g_strdup_printf(_("%u+ matches (approx)"), total_matches);
	else
		text = g_strdup_printf(_("%u matches"), total_matches);
	gtk_label_set_text(GTK_LABEL(label), text);
}

static void lds_terminal_on_search_entry_changed(GtkEditable *editable, LdsTerminal *terminal) {
	(void)editable;
	if (!terminal)
		return;

	lds_terminal_search_schedule_debounce(terminal, lds_terminal_search_debounce_cb);
}

static void lds_terminal_on_search_entry_activate(GtkEntry *entry, LdsTerminal *terminal) {
	(void)entry;
	if (!terminal)
		return;

	lds_terminal_search_apply(terminal, TRUE);
}

static void lds_terminal_on_search_next(GtkButton *button, LdsTerminal *terminal) {
	(void)button;
	if (!terminal)
		return;

	lds_terminal_search_apply(terminal, TRUE);
}

static void lds_terminal_on_search_prev(GtkButton *button, LdsTerminal *terminal) {
	(void)button;
	if (!terminal)
		return;

	lds_terminal_search_apply(terminal, FALSE);
}

static void lds_terminal_on_search_close(GtkButton *button, LdsTerminal *terminal) {
	(void)button;
	if (!terminal || !terminal->search_popover)
		return;

	gtk_popover_popdown(GTK_POPOVER(terminal->search_popover));
	lds_terminal_schedule_focus_current_term(terminal);
}

static void lds_terminal_on_search_popover_closed(GtkPopover *popover, LdsTerminal *terminal) {
	(void)popover;
	if (!terminal)
		return;

	lds_terminal_schedule_focus_current_term(terminal);
}

static gboolean lds_terminal_on_search_popover_key(GtkEventControllerKey *controller, guint keyval,
												   guint keycode, GdkModifierType state,
												   LdsTerminal *terminal) {
	(void)controller;
	(void)keycode;
	(void)state;

	if (keyval == GDK_KEY_Escape) {
		lds_terminal_on_search_close(NULL, terminal);
		return TRUE;
	}

	return FALSE;
}

static void lds_terminal_on_search_toggle(GtkToggleButton *button, LdsTerminal *terminal) {
	(void)button;
	if (!terminal)
		return;

	if (terminal->search_debounce_id) {
		g_source_remove(terminal->search_debounce_id);
		terminal->search_debounce_id = 0;
	}

	lds_terminal_search_update_regex(terminal);
}

gboolean lds_terminal_search_debounce_cb(gpointer data) {
	LdsTerminal *terminal = data;
	if (!terminal)
		return G_SOURCE_REMOVE;

	terminal->search_debounce_id = 0;
	lds_terminal_search_update_regex(terminal);
	return G_SOURCE_REMOVE;
}
