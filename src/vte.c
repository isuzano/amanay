/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * VTE integration and terminal widget behavior.
 */

/* VTE integration and tab labeling. */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gio/gio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if VTE_CHECK_VERSION(0, 46, 0)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

#include "vte.h"
#include "settings.h"
#include "menu.h"
#include "tabs.h"
#include "window.h"
#include "internal/lds_terminal_internal.h"
#include "internal/term_registry.h"
#include "internal/vte_compat.h"
#include "internal/vte_links.h"
#include "internal/vte_panes.h"
#include "internal/vte_spawn.h"

gboolean lds_terminal_on_vte_key_pressed(GtkEventControllerKey *controller, guint keyval,
										 guint keycode, GdkModifierType state,
										 LdsTerminalTerm *term);
static void lds_terminal_on_title_changed(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_title_notify(VteTerminal *vte, GParamSpec *pspec, LdsTerminalTerm *term);
void lds_terminal_on_cwd_changed(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_vte_selection_notify(VteTerminal *vte, GParamSpec *pspec,
										  LdsTerminalTerm *term);
static void lds_terminal_update_tab_label(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_eof(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_child_exited(VteTerminal *vte, gint status, LdsTerminalTerm *term);
void lds_terminal_on_secondary_child_exited(VteTerminal *vte, gint status,
											LdsTerminalTerm *term);
void lds_terminal_on_secondary_eof(VteTerminal *vte, LdsTerminalTerm *term);
void lds_terminal_on_spawn_ready(VteTerminal *vte, GPid pid, GError *error, gpointer user_data);
static gboolean lds_terminal_vte_has_selection(LdsTerminalTerm *term);
static gboolean lds_terminal_vte_clipboard_has_text(LdsTerminalTerm *term);
static gboolean lds_terminal_vte_widget_has_running_job(GtkWidget *widget, pid_t shell_pid);
static gchar *lds_terminal_truncate_label(const gchar *text, gsize max_chars);
static void lds_terminal_vte_resolve_cwd(VteTerminal *vte, gchar **out_label_text,
										 gchar **out_display_text);
static PangoFontDescription *lds_terminal_get_cached_font_desc(void);
static void lds_terminal_clear_cached_font_desc(void);
static void lds_terminal_build_palette_with_synced_prompt(const GdkRGBA *fg,
														  GdkRGBA out_palette[16]);
static gboolean lds_terminal_vte_map_scrollback_get(GValue *value, GVariant *variant,
													gpointer user_data);
static gboolean lds_terminal_vte_map_cursor_shape_get(GValue *value, GVariant *variant,
													  gpointer user_data);
static gboolean lds_terminal_vte_map_cursor_blink_get(GValue *value, GVariant *variant,
													  gpointer user_data);
static void lds_terminal_vte_bind_setting_if_present(GSettings *settings, const char *key,
													 VteTerminal *vte, const char *property,
													 GSettingsBindGetMapping get_mapping);
void lds_terminal_vte_bind_gsettings(VteTerminal *vte);
static void lds_terminal_vte_apply_settings_to_widget(VteTerminal *vte,
													  const PangoFontDescription *font,
													  gdouble scale,
													  const GdkRGBA *fg,
													  const GdkRGBA *bg,
													  const GdkRGBA *palette,
													  gsize palette_len);
static gboolean lds_terminal_vte_handle_ctrl_shift_shortcut(LdsTerminalTerm *term, guint keyval);
static gboolean lds_terminal_vte_post_split_refresh_cb(gpointer data);
void lds_terminal_vte_schedule_post_split_refresh_pass(GtkWidget *vte, pid_t shell_pid,
													   guint pass);
static gchar *lds_terminal_path_with_home_shortcut(const gchar *path, const gchar *home_prefix);

typedef struct {
	GtkWidget *vte;
	pid_t shell_pid;
	guint pass;
} LdsTerminalPostSplitRefreshData;

typedef struct {
	LdsTerminalTerm *term;
	guint token;
	gboolean timeout_source;
	guint source_id;
} LdsTerminalPostCloseFocusData;

static gchar *cached_font_name = NULL;
static PangoFontDescription *cached_font_desc = NULL;

/**
 * lds_terminal_vte_create_term:
 * @terminal: (not nullable): Terminal instance.
 * @label: (nullable): Optional tab label.
 * @cwd: (nullable): Working directory.
 * @env: (nullable) (array zero-terminated=1) (element-type utf8): Environment.
 * @exec: (nullable) (array zero-terminated=1) (element-type utf8): Command.
 *
 * Create a VTE-backed terminal tab.
 *
 * Returns: (transfer full) (nullable): A new #LdsTerminalTerm instance.
 */
LdsTerminalTerm *lds_terminal_vte_create_term(LdsTerminal *terminal, const char *label,
											  const char *cwd, char **env, char **exec) {
	g_return_val_if_fail(terminal != NULL, NULL);

	LdsTerminalTerm *term = g_new0(LdsTerminalTerm, 1);
	term->parent = terminal;
	term->closing = FALSE;
	term->spawn_failed = FALSE;
	term->link_cache = lds_link_line_cache_new(512u);
	term->link_cache_generation = 1;
	term->page = NULL;
	term->pane_split = NULL;
	term->secondary_vte = NULL;
	term->secondary_pid = -1;

	term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	term->pane_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(term->pane_split, TRUE);
	gtk_widget_set_vexpand(term->pane_split, TRUE);
	gtk_box_append(GTK_BOX(term->box), term->pane_split);
	gtk_widget_set_visible(term->pane_split, TRUE);

	term->vte = lds_terminal_vte_terminal_new();
	term->context_menu = NULL;
	term->context_actions = NULL;

	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_widget_set_hexpand(term->vte, TRUE);
	gtk_widget_set_vexpand(term->vte, TRUE);

	gtk_widget_set_visible(term->vte, TRUE);
	gtk_widget_set_visible(term->box, TRUE);

	lds_terminal_vte_terminal_set_backspace_binding(VTE_TERMINAL(term->vte), VTE_ERASE_AUTO);

	lds_terminal_vte_terminal_set_delete_binding(VTE_TERMINAL(term->vte), VTE_ERASE_AUTO);

	lds_terminal_vte_links_setup_regex(VTE_TERMINAL(term->vte));
	lds_terminal_vte_bind_gsettings(VTE_TERMINAL(term->vte));

	lds_terminal_vte_attach_controllers(term, term->vte, FALSE);
	if (!lds_terminal_vte_spawn_child(term, term->vte, FALSE, cwd, env, exec,
									  lds_terminal_on_spawn_ready, term))
		term->spawn_failed = TRUE;

	if (label) {
		gchar *seq = g_strdup_printf("\033]0;%s\007", label);
		lds_terminal_vte_terminal_feed(VTE_TERMINAL(term->vte), seq, -1);
		g_free(seq);
	}

	lds_terminal_vte_apply_settings(terminal, term);

	return term;
}

/**
 * lds_terminal_vte_free_term:
 *
 * Free a terminal term instance.
 */
void lds_terminal_vte_free_term(LdsTerminalTerm *term) {
	if (!term)
		return;

	if (term->parent && term->parent->menu_paste_cache_term == term) {
		term->parent->menu_paste_cache_term = NULL;
		term->parent->menu_paste_cache_valid = FALSE;
	}

	const gboolean ui_teardown = term->parent && term->parent->destroyed;

	term->post_close_focus_token++;
	if (term->post_close_focus_idle_id) {
		g_source_remove(term->post_close_focus_idle_id);
		term->post_close_focus_idle_id = 0;
	}
	if (term->post_close_focus_timeout_id) {
		g_source_remove(term->post_close_focus_timeout_id);
		term->post_close_focus_timeout_id = 0;
	}

	if (term->secondary_pid > 0)
		lds_terminal_terminate_child_process(term->secondary_pid);
	if (term->pid > 0)
		lds_terminal_terminate_child_process(term->pid);

	if (ui_teardown) {
		/*
		 * During final UI teardown GTK may already be destroying the widget tree.
		 * Avoid structural pane surgery here; just sever owned pointers and let
		 * GTK finish disposal of the widget hierarchy.
		 */
		term->secondary_vte = NULL;
		term->secondary_pid = -1;
		lds_terminal_vte_links_cleanup_term(term, TRUE);
		if (term->link_cache) {
			lds_link_line_cache_free(term->link_cache);
			term->link_cache = NULL;
		}

		g_clear_pointer(&term->custom_tab_title, g_free);
		term->box = NULL;
		term->vte = NULL;
		term->page = NULL;
		g_free(term);
		return;
	}

	lds_terminal_vte_remove_secondary(term, TRUE);
	lds_terminal_vte_links_cleanup_term(term, FALSE);
	if (term->link_cache) {
		lds_link_line_cache_free(term->link_cache);
		term->link_cache = NULL;
	}
	if (term->box && GTK_IS_WIDGET(term->box) && gtk_widget_get_parent(term->box))
		gtk_widget_unparent(term->box);

	term->box = NULL;
	term->vte = NULL;
	term->page = NULL;
	g_free(term);
}

/**
 * lds_terminal_vte_apply_settings:
 *
 * Apply settings to a VTE terminal.
 */
void lds_terminal_vte_apply_settings(LdsTerminal *terminal, LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return;

	VteTerminal *primary = VTE_TERMINAL(term->vte);
	VteTerminal *secondary = term->secondary_vte ? VTE_TERMINAL(term->secondary_vte) : NULL;
	gdouble scale = 1.0;
	if (terminal && terminal->scale > 0.0)
		scale = terminal->scale;

#if VTE_CHECK_VERSION(0, 38, 0)
	PangoFontDescription *font = lds_terminal_get_cached_font_desc();
#else
	PangoFontDescription *font = NULL;
#endif

	const GdkRGBA *fg = lds_terminal_settings_font_color();
	const GdkRGBA *bg = lds_terminal_settings_background_color();
	if (fg && bg) {
		GdkRGBA effective_fg = *fg;
		GdkRGBA effective_bg = *bg;
		effective_fg.alpha = 1.0;
		effective_bg.alpha = 1.0;

		if (lds_terminal_settings_sync_prompt_colors()) {
			GdkRGBA palette[16];
			lds_terminal_build_palette_with_synced_prompt(&effective_fg, palette);
			lds_terminal_vte_apply_settings_to_widget(primary, font, scale, &effective_fg,
													  &effective_bg, palette, G_N_ELEMENTS(palette));
			if (secondary)
				lds_terminal_vte_apply_settings_to_widget(secondary, font, scale, &effective_fg,
														  &effective_bg, palette,
														  G_N_ELEMENTS(palette));
		} else {
			lds_terminal_vte_apply_settings_to_widget(primary, font, scale, &effective_fg,
													  &effective_bg, NULL, 0);
			if (secondary)
				lds_terminal_vte_apply_settings_to_widget(secondary, font, scale, &effective_fg,
														  &effective_bg, NULL, 0);
		}
	} else {
		lds_terminal_vte_apply_settings_to_widget(primary, font, scale, NULL, NULL, NULL, 0);
		if (secondary)
			lds_terminal_vte_apply_settings_to_widget(secondary, font, scale, NULL, NULL, NULL, 0);
	}
}

static void lds_terminal_vte_apply_settings_to_widget(VteTerminal *vte,
													  const PangoFontDescription *font,
													  gdouble scale,
													  const GdkRGBA *fg,
													  const GdkRGBA *bg,
													  const GdkRGBA *palette,
													  gsize palette_len) {
	if (!vte)
		return;

#if VTE_CHECK_VERSION(0, 38, 0)
	lds_terminal_vte_terminal_set_font(vte, font);
	vte_terminal_set_font_scale(vte, scale);
#else
	(void)font;
	(void)scale;
	lds_terminal_vte_terminal_set_font_from_string(vte, lds_terminal_settings_font_name());
#endif

	lds_terminal_vte_terminal_set_scrollback_lines(vte, lds_terminal_settings_scrollback());
	lds_terminal_vte_terminal_set_cursor_blink_mode(
		vte, lds_terminal_settings_cursor_blink() ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);
	lds_terminal_vte_terminal_set_cursor_shape(
		vte, (VteCursorShape)lds_terminal_settings_cursor_shape());

	if (fg && bg) {
		vte_terminal_set_colors(vte, fg, bg, palette, palette_len);
		if (palette) {
			vte_terminal_set_bold_is_bright(vte, TRUE);
			vte_terminal_set_color_bold(vte, fg);
		} else {
			vte_terminal_set_color_bold(vte, NULL);
		}
	}
}

static gboolean lds_terminal_vte_map_scrollback_get(GValue *value, GVariant *variant,
													gpointer user_data) {
	(void)user_data;
	guint lines = 0;

	if (g_variant_is_of_type(variant, G_VARIANT_TYPE_UINT32))
		lines = g_variant_get_uint32(variant);
	else if (g_variant_is_of_type(variant, G_VARIANT_TYPE_INT32))
		lines = (guint)MAX(g_variant_get_int32(variant), 0);
	else
		return FALSE;

	if (G_VALUE_HOLDS_LONG(value))
		g_value_set_long(value, (glong)lines);
	else if (G_VALUE_HOLDS_INT(value))
		g_value_set_int(value, (gint)lines);
	else if (G_VALUE_HOLDS_UINT(value))
		g_value_set_uint(value, lines);
	else
		return FALSE;

	return TRUE;
}

static gboolean lds_terminal_vte_map_cursor_shape_get(GValue *value, GVariant *variant,
													  gpointer user_data) {
	(void)user_data;
	gint shape = 0;

	if (g_variant_is_of_type(variant, G_VARIANT_TYPE_INT32))
		shape = g_variant_get_int32(variant);
	else if (g_variant_is_of_type(variant, G_VARIANT_TYPE_UINT32))
		shape = (gint)g_variant_get_uint32(variant);
	else
		return FALSE;

	switch ((VteCursorShape)shape) {
	case VTE_CURSOR_SHAPE_BLOCK:
	case VTE_CURSOR_SHAPE_IBEAM:
	case VTE_CURSOR_SHAPE_UNDERLINE:
		break;
	default:
		shape = VTE_CURSOR_SHAPE_IBEAM;
		break;
	}

	if (G_VALUE_HOLDS_ENUM(value))
		g_value_set_enum(value, shape);
	else if (G_VALUE_HOLDS_INT(value))
		g_value_set_int(value, shape);
	else
		return FALSE;

	return TRUE;
}

static gboolean lds_terminal_vte_map_cursor_blink_get(GValue *value, GVariant *variant,
													  gpointer user_data) {
	(void)user_data;
	if (!g_variant_is_of_type(variant, G_VARIANT_TYPE_BOOLEAN))
		return FALSE;

	VteCursorBlinkMode mode =
		g_variant_get_boolean(variant) ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF;

	if (G_VALUE_HOLDS_ENUM(value))
		g_value_set_enum(value, mode);
	else if (G_VALUE_HOLDS_INT(value))
		g_value_set_int(value, mode);
	else
		return FALSE;

	return TRUE;
}

static void lds_terminal_vte_bind_setting_if_present(GSettings *settings, const char *key,
													 VteTerminal *vte, const char *property,
													 GSettingsBindGetMapping get_mapping) {
	if (!settings || !vte || !property)
		return;

	if (!g_object_class_find_property(G_OBJECT_GET_CLASS(vte), property))
		return;

	g_settings_bind_with_mapping(settings, key, vte, property, G_SETTINGS_BIND_GET, get_mapping,
								 NULL, NULL, NULL);
}

void lds_terminal_vte_bind_gsettings(VteTerminal *vte) {
	GSettings *backend = lds_terminal_settings_backend();
	if (!backend || !vte)
		return;

	/* Lightweight per-VTE bindings for frequently adjusted terminal properties. */
	lds_terminal_vte_bind_setting_if_present(backend, "scrollback", vte, "scrollback-lines",
											 lds_terminal_vte_map_scrollback_get);
	lds_terminal_vte_bind_setting_if_present(backend, "cursor-shape", vte, "cursor-shape",
											 lds_terminal_vte_map_cursor_shape_get);
	lds_terminal_vte_bind_setting_if_present(backend, "cursor-blink", vte, "cursor-blink-mode",
											 lds_terminal_vte_map_cursor_blink_get);
}

static void lds_terminal_build_palette_with_synced_prompt(const GdkRGBA *fg,
														  GdkRGBA out_palette[16]) {
	static const GdkRGBA base_palette[16] = {
		{0.0000, 0.0000, 0.0000, 1.0}, /* black */
		{0.8039, 0.0000, 0.0000, 1.0}, /* red */
		{0.0000, 0.8039, 0.0000, 1.0}, /* green */
		{0.8039, 0.8039, 0.0000, 1.0}, /* yellow */
		{0.0000, 0.0000, 0.9333, 1.0}, /* blue */
		{0.8039, 0.0000, 0.8039, 1.0}, /* magenta */
		{0.0000, 0.8039, 0.8039, 1.0}, /* cyan */
		{0.8980, 0.8980, 0.8980, 1.0}, /* white */
		{0.4980, 0.4980, 0.4980, 1.0}, /* bright black */
		{1.0000, 0.0000, 0.0000, 1.0}, /* bright red */
		{0.0000, 1.0000, 0.0000, 1.0}, /* bright green */
		{1.0000, 1.0000, 0.0000, 1.0}, /* bright yellow */
		{0.3608, 0.3608, 1.0000, 1.0}, /* bright blue */
		{1.0000, 0.0000, 1.0000, 1.0}, /* bright magenta */
		{0.0000, 1.0000, 1.0000, 1.0}, /* bright cyan */
		{1.0000, 1.0000, 1.0000, 1.0}  /* bright white */
	};

	memcpy(out_palette, base_palette, sizeof(base_palette));

	if (!fg)
		return;

	/*
	 * Trade-off: remap common prompt ANSI colors to foreground so prompts remain
	 * readable with custom palettes. This also affects command output that uses
	 * these same ANSI color slots.
	 */
	out_palette[2] = *fg;
	out_palette[10] = *fg;

	out_palette[4] = *fg;
	out_palette[12] = *fg;
}

void lds_terminal_vte_clear_cached_font_desc(void) {
	lds_terminal_clear_cached_font_desc();
}

/**
 * lds_terminal_vte_copy:
 *
 * Copy selection to clipboard.
 */
gboolean lds_terminal_vte_copy(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	if (!lds_terminal_vte_has_selection(term))
		return FALSE;

	lds_terminal_vte_terminal_copy_clipboard_format(VTE_TERMINAL(active), VTE_FORMAT_TEXT);
	return TRUE;
}

/**
 * lds_terminal_vte_paste:
 *
 * Paste clipboard content.
 */
gboolean lds_terminal_vte_paste(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	if (!lds_terminal_vte_clipboard_has_text(term))
		return FALSE;

	lds_terminal_vte_terminal_paste_clipboard(VTE_TERMINAL(active));
	return TRUE;
}

gboolean lds_terminal_vte_clear(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	const gboolean secondary_active = term->secondary_vte && active == term->secondary_vte;
	pid_t active_pid = secondary_active ? term->secondary_pid : term->pid;
	if (active_pid > 0) {
		/* Ctrl+L keeps shell session and performs a real clear. */
		vte_terminal_feed_child(VTE_TERMINAL(active), "\f", 1);
		return TRUE;
	}

	vte_terminal_reset(VTE_TERMINAL(active), FALSE, TRUE);
	return TRUE;
}

gboolean lds_terminal_vte_reset(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	/* Reset terminal state/buffer without injecting visible shell commands. */
	vte_terminal_reset(VTE_TERMINAL(active), TRUE, TRUE);

	const gboolean secondary_active = term->secondary_vte && active == term->secondary_vte;
	pid_t active_pid = secondary_active ? term->secondary_pid : term->pid;
	if (active_pid > 0) {
		/* Ctrl+L clear to present a clean prompt right after reset. */
		vte_terminal_feed_child(VTE_TERMINAL(active), "\f", 1);
	}

	return TRUE;
}

void lds_terminal_on_child_exited(VteTerminal *vte, gint status, LdsTerminalTerm *term) {
	(void)vte;
	(void)status;

	if (!lds_terminal_vte_term_alive(term))
		return;

	if (term->secondary_vte && lds_terminal_vte_promote_secondary(term, GTK_WIDGET(vte)))
		return;

	lds_terminal_tabs_close(term->parent, term, LDS_TERMINAL_TABS_CLOSE_BY_CHILD);
}

gboolean lds_terminal_on_vte_key_pressed(GtkEventControllerKey *controller, guint keyval,
										 guint keycode, GdkModifierType state,
										 LdsTerminalTerm *term) {
	(void)keycode;
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	(void)widget;

	const gboolean ctrl_shift =
		((state & GDK_CONTROL_MASK) != 0) && ((state & GDK_SHIFT_MASK) != 0);

	if (!ctrl_shift || !lds_terminal_vte_term_alive(term))
		return FALSE;

	return lds_terminal_vte_handle_ctrl_shift_shortcut(term, keyval);
}

void lds_terminal_on_eof(VteTerminal *vte, LdsTerminalTerm *term) {
	(void)vte;

	if (!lds_terminal_vte_term_alive(term))
		return;

	lds_terminal_tabs_close(term->parent, term, LDS_TERMINAL_TABS_CLOSE_BY_EOF);
}

void lds_terminal_on_secondary_child_exited(VteTerminal *vte, gint status,
											LdsTerminalTerm *term) {
	(void)status;
	if (!lds_terminal_vte_term_alive(term))
		return;

	lds_terminal_vte_close_secondary_widget(term, GTK_WIDGET(vte));
}

void lds_terminal_on_secondary_eof(VteTerminal *vte, LdsTerminalTerm *term) {
	if (!lds_terminal_vte_term_alive(term))
		return;

	lds_terminal_vte_close_secondary_widget(term, GTK_WIDGET(vte));
}

void lds_terminal_on_spawn_ready(VteTerminal *vte, GPid pid, GError *error,
								 gpointer user_data) {
	LdsTerminalTerm *term = user_data;
	if (!term)
		return;

	if (lds_terminal_vte_is_secondary_widget(term, GTK_WIDGET(vte))) {
		lds_terminal_vte_handle_secondary_spawn_ready(term, pid, error);
		return;
	}

	lds_terminal_vte_handle_spawn_ready(term, pid, error);
}

void lds_terminal_vte_handle_spawn_ready(LdsTerminalTerm *term, GPid pid, GError *error) {
	if (!term)
		return;
	if (!lds_terminal_vte_term_is_owned(term)) {
		if (!error && pid > 0)
			lds_terminal_terminate_child_process(pid);
		return;
	}

	if (error) {
		g_warning("Failed to spawn terminal child: %s", error->message);
		term->spawn_failed = TRUE;

		if (term->closing) {
			lds_terminal_remove_term(term->parent, term);
			return;
		}

		lds_terminal_tabs_close(term->parent, term, LDS_TERMINAL_TABS_CLOSE_BY_SPAWN_ERROR);
		return;
	}

	if (term->closing) {
		if (pid > 0)
			lds_terminal_terminate_child_process(pid);

		lds_terminal_remove_term(term->parent, term);
		return;
	}

	term->pid = pid;
}

static gboolean lds_terminal_vte_has_selection(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	return vte_terminal_get_has_selection(VTE_TERMINAL(active));
}

static gboolean lds_terminal_vte_clipboard_has_text(LdsTerminalTerm *term) {
	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	GdkDisplay *display = gtk_widget_get_display(active);
	if (!display)
		return FALSE;

	GdkClipboard *clipboard = gdk_display_get_clipboard(display);
	if (!clipboard)
		return FALSE;

	const GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard);
	if (!formats)
		return FALSE;

	if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING))
		return TRUE;

	if (gdk_content_formats_contain_mime_type(formats, "text/plain"))
		return TRUE;

	if (gdk_content_formats_contain_mime_type(formats, "text/plain;charset=utf-8"))
		return TRUE;

	return FALSE;
}

static void lds_terminal_on_title_changed(VteTerminal *vte, LdsTerminalTerm *term) {
	lds_terminal_update_tab_label(vte, term);
}

void lds_terminal_on_title_notify(VteTerminal *vte, GParamSpec *pspec,
								  LdsTerminalTerm *term) {
	(void)pspec;
	lds_terminal_on_title_changed(vte, term);
}

void lds_terminal_on_cwd_changed(VteTerminal *vte, LdsTerminalTerm *term) {
	lds_terminal_update_tab_label(vte, term);
}

void lds_terminal_on_vte_selection_notify(VteTerminal *vte, GParamSpec *pspec,
										  LdsTerminalTerm *term) {
	(void)pspec;
	if (!lds_terminal_vte_term_alive(term))
		return;

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active || GTK_WIDGET(vte) != active)
		return;

	lds_terminal_menu_sync_edit_actions(term->parent);
}

static void lds_terminal_update_tab_label(VteTerminal *vte, LdsTerminalTerm *term) {
	if (!lds_terminal_vte_term_alive(term) || !term->box)
		return;

	g_autofree gchar *label_text = NULL;
	g_autofree gchar *ui_text = NULL;
	g_autofree gchar *tooltip_text = NULL;
	g_autofree gchar *display_text = NULL;

	if (term->custom_tab_title && *term->custom_tab_title) {
		ui_text = lds_terminal_truncate_label(term->custom_tab_title, 30);
		tooltip_text = g_strdup(term->custom_tab_title);

		if (term->page)
			adw_tab_page_set_title(term->page, ui_text);

		if (term->parent && term->parent->tab_view && term->page &&
			adw_tab_view_get_selected_page(ADW_TAB_VIEW(term->parent->tab_view)) == term->page)
			lds_terminal_window_update_title(term->parent, ui_text);

		if (term->page)
			adw_tab_page_set_tooltip(term->page, tooltip_text);
		return;
	}

	lds_terminal_vte_resolve_cwd(vte, &label_text, &display_text);

	/* Fallback to title when VTE does not provide cwd. */
	if (!label_text) {
		g_autofree gchar *title = NULL;
		g_object_get(vte, "window-title", &title, NULL);
		if (title && *title)
			label_text = g_strdup(title);
	}

	if (!label_text)
		return;

	const gchar *home = g_get_home_dir();
	const gchar *user = g_get_user_name();
	g_autofree gchar *home_prefix = NULL;
	g_autofree gchar *full_title = NULL;
	g_autofree gchar *home_short = NULL;

	if (home && *home) {
		home_prefix = g_strdup(home);
	} else if (user && *user) {
		home_prefix = g_strdup_printf("/home/%s", user);
	}

	home_short = lds_terminal_path_with_home_shortcut(label_text, home_prefix);
	if (display_text) {
		tooltip_text = g_strdup(display_text);
		full_title = g_strdup(display_text);
	} else {
		full_title = g_strdup(home_short);
		tooltip_text = g_strdup(home_short);
	}

	ui_text = lds_terminal_truncate_label(full_title, 30);
	if (term->page)
		adw_tab_page_set_title(term->page, ui_text);

	if (term->parent && term->parent->tab_view && term->page &&
		adw_tab_view_get_selected_page(ADW_TAB_VIEW(term->parent->tab_view)) == term->page)
		lds_terminal_window_update_title(term->parent, ui_text);

	if (term->page && tooltip_text)
		adw_tab_page_set_tooltip(term->page, tooltip_text);
}

void lds_terminal_vte_refresh_tab_label(LdsTerminalTerm *term) {
	if (!term || !term->vte || !VTE_IS_TERMINAL(term->vte))
		return;

	lds_terminal_update_tab_label(VTE_TERMINAL(term->vte), term);
}

static void lds_terminal_vte_resolve_cwd(VteTerminal *vte, gchar **out_label_text,
										 gchar **out_display_text) {
	if (!vte || !out_label_text || !out_display_text)
		return;

	*out_label_text = NULL;
	*out_display_text = NULL;

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(vte), "current-directory-uri")) {
		g_autofree gchar *uri = NULL;
		g_object_get(vte, "current-directory-uri", &uri, NULL);
		if (uri && *uri) {
			g_autoptr(GError) uri_error = NULL;
			GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, &uri_error);
			if (parsed) {
				const gchar *scheme = g_uri_get_scheme(parsed);
				const gchar *host = g_uri_get_host(parsed);
				const gchar *userinfo = g_uri_get_userinfo(parsed);
				const gchar *path = g_uri_get_path(parsed);

				if (scheme && g_strcmp0(scheme, "file") == 0 && path) {
					*out_label_text = g_strdup(path);
				} else if (path) {
					const gchar *user = NULL;
					if (userinfo && *userinfo)
						user = userinfo;

					if (host && *host) {
						if (user)
							*out_display_text = g_strdup_printf("%s@%s:%s", user, host, path);
						else
							*out_display_text = g_strdup_printf("%s:%s", host, path);
					} else {
						*out_display_text = g_strdup(path);
					}

					*out_label_text = g_strdup(path);
				}
				g_uri_unref(parsed);
			} else {
				*out_label_text = g_uri_unescape_string(uri, NULL);
			}
		}
	}

	if (*out_label_text)
		return;

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(vte), "current-directory")) {
		g_autofree gchar *cwd = NULL;
		g_object_get(vte, "current-directory", &cwd, NULL);
		if (cwd && *cwd)
			*out_label_text = g_strdup(cwd);
	}
}

static gchar *lds_terminal_truncate_label(const gchar *text, gsize max_chars) {
	if (!text || !*text)
		return g_strdup(LDS_TERMINAL_DISPLAY_NAME);

	if (max_chars == 0)
		return g_strdup(text);

	gsize len = (gsize)g_utf8_strlen(text, -1);
	if (len <= max_chars)
		return g_strdup(text);

	gsize keep = max_chars > 3 ? max_chars - 3 : max_chars;
	g_autofree gchar *tmp = g_utf8_substring(text, 0, (glong)keep);
	return g_strconcat(tmp, "...", NULL);
}

static gboolean lds_terminal_vte_handle_ctrl_shift_shortcut(LdsTerminalTerm *term, guint keyval) {
	if (!term)
		return FALSE;

	if (keyval == GDK_KEY_C || keyval == GDK_KEY_c)
		return lds_terminal_vte_copy(term);

	if (keyval == GDK_KEY_V || keyval == GDK_KEY_v)
		return lds_terminal_vte_paste(term);

	return FALSE;
}

gboolean lds_terminal_vte_active_has_selection(LdsTerminalTerm *term) {
	return lds_terminal_vte_has_selection(term);
}

gboolean lds_terminal_vte_active_clipboard_has_text(LdsTerminalTerm *term) {
	return lds_terminal_vte_clipboard_has_text(term);
}

static gboolean lds_terminal_vte_widget_has_running_job(GtkWidget *widget, pid_t shell_pid) {
	if (!widget || !VTE_IS_TERMINAL(widget))
		return FALSE;

	VtePty *pty = vte_terminal_get_pty(VTE_TERMINAL(widget));
	if (!pty)
		return FALSE;

	int pty_fd = vte_pty_get_fd(pty);
	if (pty_fd < 0)
		return FALSE;

	errno = 0;
	pid_t fg_pgrp = tcgetpgrp(pty_fd);
	if (fg_pgrp <= 0)
		return FALSE;

	/* If fg group differs from tracked shell pid, a foreground job is running. */
	if (shell_pid > 0)
		return fg_pgrp != shell_pid;

	return TRUE;
}

gboolean lds_terminal_vte_active_has_running_job(LdsTerminalTerm *term) {
	if (!term)
		return FALSE;

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	const gboolean secondary_active = term->secondary_vte && active == term->secondary_vte;
	pid_t shell_pid = secondary_active ? term->secondary_pid : term->pid;
	return lds_terminal_vte_widget_has_running_job(active, shell_pid);
}

gboolean lds_terminal_vte_term_has_running_jobs(LdsTerminalTerm *term) {
	return lds_terminal_vte_term_running_job_count(term) > 0;
}

guint lds_terminal_vte_term_running_job_count(LdsTerminalTerm *term) {
	if (!term)
		return 0;

	guint count = 0;
	if (lds_terminal_vte_widget_has_running_job(term->vte, term->pid))
		count++;
	if (term->secondary_vte && lds_terminal_vte_has_split(term) &&
		lds_terminal_vte_widget_has_running_job(term->secondary_vte, term->secondary_pid)) {
		count++;
	}

	return count;
}

static gboolean lds_terminal_vte_post_split_refresh_cb(gpointer data) {
	LdsTerminalPostSplitRefreshData *refresh = data;
	if (!refresh) {
		return G_SOURCE_REMOVE;
	}

	GtkWidget *vte = refresh->vte;
	if (!vte || !VTE_IS_TERMINAL(vte)) {
		if (vte)
			g_object_unref(vte);
		g_free(refresh);
		return G_SOURCE_REMOVE;
	}

	VteTerminal *terminal = VTE_TERMINAL(vte);

	gtk_widget_queue_resize(vte);
	gtk_widget_queue_draw(vte);

	glong cols = vte_terminal_get_column_count(terminal);
	glong rows = vte_terminal_get_row_count(terminal);
	if (cols > 0 && rows > 0)
		vte_terminal_set_size(terminal, cols, rows);

	pid_t fg_pgrp = -1;
	VtePty *pty = vte_terminal_get_pty(terminal);
	int pty_fd = -1;
	if (pty)
		pty_fd = vte_pty_get_fd(pty);

	if (pty_fd >= 0) {
		if (cols > 0 && rows > 0) {
			struct winsize ws = {0};
			if (ioctl(pty_fd, TIOCGWINSZ, &ws) == 0) {
				if ((glong)ws.ws_col != cols || (glong)ws.ws_row != rows) {
					ws.ws_col = (unsigned short)cols;
					ws.ws_row = (unsigned short)rows;
					if (ioctl(pty_fd, TIOCSWINSZ, &ws) != 0) {
						g_warning("Failed to force PTY winsize sync: %s", g_strerror(errno));
					}
				}
			}
		}

		errno = 0;
		fg_pgrp = tcgetpgrp(pty_fd);
		if (fg_pgrp > 0) {
			if (kill(-fg_pgrp, SIGWINCH) != 0) {
				int saved_errno = errno;
				if (saved_errno != ESRCH && saved_errno != EPERM) {
					g_warning("Failed to send SIGWINCH to fg pgrp %d", fg_pgrp);
				}
			}
		} else if (fg_pgrp < 0) {
			int saved_errno = errno;
			/* PTY may be gone/not controlling tty during teardown races. */
			if (saved_errno != ENOTTY && saved_errno != EIO && saved_errno != EBADF) {
				g_message("Failed to query fg pgrp for SIGWINCH refresh: %s",
						  g_strerror(saved_errno));
			}
		}
	}

	if (refresh->shell_pid > 0 && refresh->shell_pid != fg_pgrp) {
		if (kill(refresh->shell_pid, SIGWINCH) != 0) {
			int saved_errno = errno;
			if (saved_errno != ESRCH && saved_errno != EPERM) {
				g_warning("Failed to send SIGWINCH to shell pid %d", refresh->shell_pid);
			}
		}
	}

	if (refresh->pass == 0) {
		lds_terminal_vte_schedule_post_split_refresh_pass(vte, refresh->shell_pid, 1);
	}

	g_object_unref(vte);
	g_free(refresh);
	return G_SOURCE_REMOVE;
}

void lds_terminal_vte_schedule_post_split_refresh_pass(GtkWidget *vte, pid_t shell_pid,
													   guint pass) {
	if (!vte || !VTE_IS_TERMINAL(vte))
		return;

	LdsTerminalPostSplitRefreshData *refresh = g_new0(LdsTerminalPostSplitRefreshData, 1);
	refresh->vte = g_object_ref(vte);
	refresh->shell_pid = shell_pid;
	refresh->pass = pass;
	if (pass == 0) {
		g_idle_add(lds_terminal_vte_post_split_refresh_cb, refresh);
	} else {
		guint delay_ms = 32;
		if (pass == 2)
			delay_ms = 50;
		if (lds_terminal_settings_strict_determinism())
			delay_ms = 1;
		g_timeout_add(delay_ms, lds_terminal_vte_post_split_refresh_cb, refresh);
	}
}

static PangoFontDescription *lds_terminal_get_cached_font_desc(void) {
	const char *current = lds_terminal_settings_font_name();

	if (!cached_font_name || g_strcmp0(cached_font_name, current) != 0) {
		g_free(cached_font_name);
		cached_font_name = g_strdup(current);

		if (cached_font_desc)
			pango_font_description_free(cached_font_desc);

		cached_font_desc = pango_font_description_from_string(current);
	}

	return cached_font_desc;
}

static void lds_terminal_clear_cached_font_desc(void) {
	g_clear_pointer(&cached_font_name, g_free);

	if (cached_font_desc) {
		pango_font_description_free(cached_font_desc);
		cached_font_desc = NULL;
	}
}

static gchar *lds_terminal_path_with_home_shortcut(const gchar *path, const gchar *home_prefix) {
	if (!path)
		return NULL;

	if (home_prefix && g_str_has_prefix(path, home_prefix)) {
		gsize prefix_len = strlen(home_prefix);
		char boundary = path[prefix_len];
		if (boundary == '\0' || boundary == '/')
			return g_strdup_printf("~%s", path + prefix_len);
	}

	return g_strdup(path);
}
