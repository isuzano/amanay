/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Terminal pane management and split behavior.
 */

/* Pane lifecycle: split, promotion, active-pane routing, and close-focus reassert. */

#include <adwaita.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "menu.h"
#include "settings.h"
#include "internal/lds_terminal_internal.h"
#include "internal/vte_compat.h"
#include "internal/vte_links.h"
#include "internal/vte_panes.h"
#include "internal/vte_spawn.h"

typedef struct {
	LdsTerminalTerm *term;
	guint token;
	gboolean timeout_source;
	guint source_id;
} LdsTerminalPostCloseFocusData;

static gboolean lds_terminal_vte_has_live_secondary(LdsTerminalTerm *term);
static void lds_terminal_vte_clear_secondary_state(LdsTerminalTerm *term);
static void lds_terminal_vte_normalize_single_pane_state(LdsTerminalTerm *term);
static gboolean lds_terminal_vte_finalize_closed_pane(LdsTerminalTerm *term);
static GtkWidget *lds_terminal_vte_create_secondary_widget(LdsTerminalTerm *term);
static void lds_terminal_vte_attach_secondary_to_pane(LdsTerminalTerm *term, GtkWidget *secondary);
static void lds_terminal_vte_detach_end_child(GtkWidget *pane, GtkWidget *child,
											  gboolean preserve_widget);
static gboolean lds_terminal_vte_post_close_focus_cb(gpointer data);
static void lds_terminal_vte_schedule_post_close_focus_reassert(LdsTerminalTerm *term);
static void lds_terminal_vte_schedule_post_split_refresh(GtkWidget *vte, pid_t shell_pid);
static void lds_terminal_vte_rebuild_single_pane_container(LdsTerminalTerm *term);

static gboolean lds_terminal_vte_has_live_secondary(LdsTerminalTerm *term) {
	if (!term || !term->secondary_vte)
		return FALSE;

	if (!term->pane_split || !GTK_IS_PANED(term->pane_split))
		return TRUE;

	return gtk_paned_get_end_child(GTK_PANED(term->pane_split)) == term->secondary_vte;
}

static void lds_terminal_vte_clear_secondary_state(LdsTerminalTerm *term) {
	if (!term)
		return;

	term->secondary_vte = NULL;
	term->secondary_pid = -1;
}

static void lds_terminal_vte_normalize_single_pane_state(LdsTerminalTerm *term) {
	if (!term || lds_terminal_vte_has_live_secondary(term))
		return;

	lds_terminal_vte_clear_secondary_state(term);
}

static gboolean lds_terminal_vte_finalize_closed_pane(LdsTerminalTerm *term) {
	if (!term)
		return FALSE;

	lds_terminal_vte_resync_layout(term);
	lds_terminal_vte_schedule_post_close_focus_reassert(term);
	lds_terminal_schedule_focus_current_term(term->parent);
	return TRUE;
}

static GtkWidget *lds_terminal_vte_create_secondary_widget(LdsTerminalTerm *term) {
	if (!term)
		return NULL;

	GtkWidget *secondary = lds_terminal_vte_terminal_new();
	gtk_widget_set_hexpand(secondary, TRUE);
	gtk_widget_set_vexpand(secondary, TRUE);
	gtk_widget_set_visible(secondary, TRUE);

	lds_terminal_vte_terminal_set_backspace_binding(VTE_TERMINAL(secondary), VTE_ERASE_AUTO);
	lds_terminal_vte_terminal_set_delete_binding(VTE_TERMINAL(secondary), VTE_ERASE_AUTO);
	lds_terminal_vte_links_setup_regex(VTE_TERMINAL(secondary));
	lds_terminal_vte_bind_gsettings(VTE_TERMINAL(secondary));
	lds_terminal_vte_attach_controllers(term, secondary, TRUE);
	return secondary;
}

static void lds_terminal_vte_attach_secondary_to_pane(LdsTerminalTerm *term, GtkWidget *secondary) {
	if (!term || !secondary || !term->pane_split || !term->vte)
		return;

	gtk_orientable_set_orientation(GTK_ORIENTABLE(term->pane_split), GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_start_child(GTK_PANED(term->pane_split), term->vte);
	gtk_paned_set_end_child(GTK_PANED(term->pane_split), secondary);
	term->secondary_vte = secondary;
	term->secondary_pid = -1;
}

static void lds_terminal_vte_detach_end_child(GtkWidget *pane, GtkWidget *child,
											  gboolean preserve_widget) {
	if (!pane || !child || !GTK_IS_PANED(pane))
		return;

	if (gtk_paned_get_end_child(GTK_PANED(pane)) != child)
		return;

	if (!preserve_widget) {
		gtk_paned_set_end_child(GTK_PANED(pane), NULL);
		return;
	}

	g_object_ref(child);
	gtk_paned_set_end_child(GTK_PANED(pane), NULL);
	g_object_unref(child);
}

gboolean lds_terminal_vte_is_secondary_widget(LdsTerminalTerm *term, GtkWidget *widget) {
	return term && widget && term->secondary_vte && widget == term->secondary_vte;
}

void lds_terminal_vte_close_secondary_widget(LdsTerminalTerm *term, GtkWidget *widget) {
	if (!lds_terminal_vte_is_secondary_widget(term, widget))
		return;

	lds_terminal_vte_remove_secondary(term, TRUE);
}

void lds_terminal_vte_handle_secondary_spawn_ready(LdsTerminalTerm *term, GPid pid, GError *error) {
	if (!term)
		return;

	if (error) {
		g_warning("Failed to spawn secondary terminal child: %s", error->message);
		lds_terminal_vte_remove_secondary(term, TRUE);
		lds_terminal_schedule_focus_current_term(term->parent);
		return;
	}

	if (term->closing) {
		if (pid > 0)
			lds_terminal_terminate_child_process(pid);
		lds_terminal_vte_remove_secondary(term, TRUE);
		return;
	}

	term->secondary_pid = pid;
}

static gboolean lds_terminal_vte_post_close_focus_cb(gpointer data) {
	LdsTerminalPostCloseFocusData *payload = data;
	if (!payload || !payload->term) {
		g_free(payload);
		return G_SOURCE_REMOVE;
	}

	LdsTerminalTerm *term = payload->term;
	if (payload->timeout_source) {
		if (term->post_close_focus_timeout_id == payload->source_id)
			term->post_close_focus_timeout_id = 0;
	} else {
		if (term->post_close_focus_idle_id == payload->source_id)
			term->post_close_focus_idle_id = 0;
	}

	if (payload->token != term->post_close_focus_token) {
		g_free(payload);
		return G_SOURCE_REMOVE;
	}

	if (!lds_terminal_vte_term_is_owned(term) || !term->vte || term->closing) {
		g_free(payload);
		return G_SOURCE_REMOVE;
	}

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		active = term->vte;

	gtk_widget_grab_focus(active);
	gtk_widget_queue_draw(active);

	g_free(payload);
	return G_SOURCE_REMOVE;
}

static void lds_terminal_vte_schedule_post_close_focus_reassert(LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return;

	if (term->post_close_focus_idle_id) {
		g_source_remove(term->post_close_focus_idle_id);
		term->post_close_focus_idle_id = 0;
	}
	if (term->post_close_focus_timeout_id) {
		g_source_remove(term->post_close_focus_timeout_id);
		term->post_close_focus_timeout_id = 0;
	}
	term->post_close_focus_token++;

	LdsTerminalPostCloseFocusData *idle_payload = g_new0(LdsTerminalPostCloseFocusData, 1);
	idle_payload->term = term;
	idle_payload->token = term->post_close_focus_token;
	idle_payload->timeout_source = FALSE;
	term->post_close_focus_idle_id = g_idle_add(lds_terminal_vte_post_close_focus_cb, idle_payload);
	idle_payload->source_id = term->post_close_focus_idle_id;

	if (!lds_terminal_settings_strict_determinism()) {
		LdsTerminalPostCloseFocusData *timeout_payload = g_new0(LdsTerminalPostCloseFocusData, 1);
		timeout_payload->term = term;
		timeout_payload->token = term->post_close_focus_token;
		timeout_payload->timeout_source = TRUE;
		term->post_close_focus_timeout_id =
			g_timeout_add(16, lds_terminal_vte_post_close_focus_cb, timeout_payload);
		timeout_payload->source_id = term->post_close_focus_timeout_id;
	}

	lds_terminal_vte_schedule_post_split_refresh_pass(term->vte, term->pid, 2);
}

static void lds_terminal_vte_schedule_post_split_refresh(GtkWidget *vte, pid_t shell_pid) {
	lds_terminal_vte_schedule_post_split_refresh_pass(vte, shell_pid, 0);
}

gboolean lds_terminal_vte_split(LdsTerminalTerm *term, GtkOrientation orientation) {
	if (!term || !term->box || !term->vte || !term->pane_split || term->closing)
		return FALSE;

	if (orientation != GTK_ORIENTATION_VERTICAL)
		return FALSE;

	lds_terminal_vte_normalize_single_pane_state(term);
	if (lds_terminal_vte_has_live_secondary(term))
		return FALSE;

	GtkWidget *secondary = lds_terminal_vte_create_secondary_widget(term);
	if (!secondary)
		return FALSE;

	lds_terminal_vte_attach_secondary_to_pane(term, secondary);
	lds_terminal_vte_apply_settings(term->parent, term);
	if (!lds_terminal_vte_spawn_child(term, secondary, TRUE, NULL, NULL, NULL,
									  lds_terminal_on_spawn_ready, term)) {
		lds_terminal_vte_remove_secondary(term, TRUE);
		lds_terminal_schedule_focus_current_term(term->parent);
		return FALSE;
	}

	gtk_widget_grab_focus(secondary);
	lds_terminal_schedule_focus_current_term(term->parent);
	lds_terminal_menu_sync_edit_actions(term->parent);
	return TRUE;
}

gboolean lds_terminal_vte_close_active_pane(LdsTerminalTerm *term) {
	if (!term || !term->vte || term->closing)
		return FALSE;

	lds_terminal_vte_normalize_single_pane_state(term);
	if (!lds_terminal_vte_has_live_secondary(term))
		return FALSE;

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		return FALSE;

	if (active == term->secondary_vte) {
		GtkWidget *secondary = term->secondary_vte;
		g_signal_handlers_disconnect_by_data(secondary, term);
		if (term->secondary_pid > 0) {
			lds_terminal_terminate_child_process(term->secondary_pid);
			term->secondary_pid = -1;
		}
		lds_terminal_vte_remove_secondary(term, TRUE);
		return lds_terminal_vte_finalize_closed_pane(term);
	}

	GtkWidget *old_primary = term->vte;
	g_signal_handlers_disconnect_by_data(old_primary, term);
	if (term->pid > 0) {
		lds_terminal_terminate_child_process(term->pid);
		term->pid = -1;
	}

	if (!lds_terminal_vte_promote_secondary(term, old_primary))
		return FALSE;

	return lds_terminal_vte_finalize_closed_pane(term);
}

void lds_terminal_vte_focus_next_pane(LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return;

	lds_terminal_vte_normalize_single_pane_state(term);
	if (!lds_terminal_vte_has_live_secondary(term))
		return;

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (active == term->secondary_vte)
		gtk_widget_grab_focus(term->vte);
	else
		gtk_widget_grab_focus(term->secondary_vte);

	lds_terminal_schedule_focus_current_term(term->parent);
	lds_terminal_menu_sync_edit_actions(term->parent);
}

gboolean lds_terminal_vte_has_split(LdsTerminalTerm *term) {
	return lds_terminal_vte_has_live_secondary(term);
}

void lds_terminal_vte_resync_layout(LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return;

	GtkWidget *active = lds_terminal_vte_get_active_widget(term);
	if (!active)
		active = term->vte;

	gtk_widget_grab_focus(active);
	lds_terminal_schedule_focus_current_term(term->parent);

	lds_terminal_vte_schedule_post_split_refresh(term->vte, term->pid);
	if (term->secondary_vte)
		lds_terminal_vte_schedule_post_split_refresh(term->secondary_vte, term->secondary_pid);

	lds_terminal_menu_sync_edit_actions(term->parent);
}

void lds_terminal_vte_attach_controllers(LdsTerminalTerm *term, GtkWidget *vte,
										 gboolean secondary) {
	g_signal_handlers_disconnect_by_data(vte, term);
	g_signal_connect(vte, "notify::has-selection", G_CALLBACK(lds_terminal_on_vte_selection_notify),
					 term);
	g_signal_connect(vte, "contents-changed",
					 G_CALLBACK(lds_terminal_vte_links_on_contents_changed), term);
	if (g_signal_lookup("text-scrolled", G_OBJECT_TYPE(vte)) != 0) {
		g_signal_connect(vte, "text-scrolled",
						 G_CALLBACK(lds_terminal_vte_links_on_text_scrolled), term);
	}
	if (secondary) {
		g_signal_connect(vte, "child-exited", G_CALLBACK(lds_terminal_on_secondary_child_exited),
						 term);
#if VTE_CHECK_VERSION(0, 38, 0)
		g_signal_connect(vte, "eof", G_CALLBACK(lds_terminal_on_secondary_eof), term);
#endif
	} else {
		g_signal_connect(vte, "child-exited", G_CALLBACK(lds_terminal_on_child_exited), term);
		g_signal_connect(vte, "notify::window-title", G_CALLBACK(lds_terminal_on_title_notify),
						 term);
#if VTE_CHECK_VERSION(0, 38, 0)
		g_signal_connect(vte, "eof", G_CALLBACK(lds_terminal_on_eof), term);
#endif
#if VTE_CHECK_VERSION(0, 46, 0)
		g_signal_connect(vte, "current-directory-uri-changed",
						 G_CALLBACK(lds_terminal_on_cwd_changed), term);
#else
		g_signal_connect(vte, "current-directory-changed", G_CALLBACK(lds_terminal_on_cwd_changed),
						 term);
#endif
	}

	GtkEventController *key = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(vte, key);
	g_signal_connect(key, "key-pressed", G_CALLBACK(lds_terminal_on_vte_key_pressed), term);

	GtkGestureClick *primary = GTK_GESTURE_CLICK(gtk_gesture_click_new());
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(primary), GDK_BUTTON_PRIMARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(primary), GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(vte, GTK_EVENT_CONTROLLER(primary));
	g_signal_connect(primary, "pressed", G_CALLBACK(lds_terminal_vte_links_on_click_primary), term);

	GtkGestureClick *secondary_click = GTK_GESTURE_CLICK(gtk_gesture_click_new());
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary_click), GDK_BUTTON_SECONDARY);
	gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(secondary_click), TRUE);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(secondary_click),
											   GTK_PHASE_BUBBLE);
	gtk_widget_add_controller(vte, GTK_EVENT_CONTROLLER(secondary_click));
	g_signal_connect(secondary_click, "pressed",
					 G_CALLBACK(lds_terminal_vte_links_on_click_secondary), term);

	GtkEventController *motion = gtk_event_controller_motion_new();
	gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(vte, motion);
	g_signal_connect(motion, "motion", G_CALLBACK(lds_terminal_vte_links_on_motion), term);
	g_signal_connect(motion, "leave", G_CALLBACK(lds_terminal_vte_links_on_leave), term);
}

GtkWidget *lds_terminal_vte_get_active_widget(LdsTerminalTerm *term) {
	if (!term || !term->vte)
		return NULL;

	if (!lds_terminal_vte_has_live_secondary(term) || !term->parent || !term->parent->window)
		return term->vte;

	GtkWidget *focus = NULL;
	if (GTK_IS_ROOT(term->parent->window))
		focus = gtk_root_get_focus(GTK_ROOT(term->parent->window));

	if (focus && GTK_IS_WIDGET(term->secondary_vte) &&
		gtk_widget_is_ancestor(focus, term->secondary_vte))
		return term->secondary_vte;

	if (focus && GTK_IS_WIDGET(term->vte) && gtk_widget_is_ancestor(focus, term->vte))
		return term->vte;

	if (term->context_menu && GTK_IS_WIDGET(term->context_menu)) {
		GtkWidget *context_vte = g_object_get_data(G_OBJECT(term->context_menu), "lds-context-vte");
		if (context_vte == term->secondary_vte)
			return term->secondary_vte;
		if (context_vte == term->vte)
			return term->vte;
	}

	return term->vte;
}

static void lds_terminal_vte_rebuild_single_pane_container(LdsTerminalTerm *term) {
	if (!term || !term->box || !term->vte || !term->pane_split)
		return;
	if (!GTK_IS_PANED(term->pane_split))
		return;

	GtkWidget *old_pane = term->pane_split;
	GtkWidget *replacement = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(replacement, TRUE);
	gtk_widget_set_vexpand(replacement, TRUE);

	g_object_ref(term->vte);
	if (gtk_paned_get_start_child(GTK_PANED(old_pane)) == term->vte)
		gtk_paned_set_start_child(GTK_PANED(old_pane), NULL);
	else if (gtk_paned_get_end_child(GTK_PANED(old_pane)) == term->vte)
		gtk_paned_set_end_child(GTK_PANED(old_pane), NULL);
	gtk_paned_set_start_child(GTK_PANED(replacement), term->vte);

	if (gtk_widget_get_parent(old_pane) == term->box)
		gtk_box_remove(GTK_BOX(term->box), old_pane);
	gtk_box_append(GTK_BOX(term->box), replacement);
	gtk_widget_set_visible(replacement, TRUE);
	gtk_widget_queue_resize(term->box);
	gtk_widget_queue_resize(replacement);

	term->pane_split = replacement;
	g_object_unref(term->vte);
}

void lds_terminal_vte_remove_secondary(LdsTerminalTerm *term, gboolean destroy_widget) {
	if (!term || !term->secondary_vte)
		return;

	GtkWidget *secondary = term->secondary_vte;
	g_signal_handlers_disconnect_by_data(secondary, term);
	if (term->pane_split && GTK_IS_PANED(term->pane_split) &&
		gtk_paned_get_end_child(GTK_PANED(term->pane_split)) == secondary) {
		lds_terminal_vte_detach_end_child(term->pane_split, secondary, !destroy_widget);
	} else if (destroy_widget && GTK_IS_WIDGET(secondary) && gtk_widget_get_parent(secondary)) {
		gtk_widget_unparent(secondary);
	}

	lds_terminal_vte_clear_secondary_state(term);
	lds_terminal_vte_rebuild_single_pane_container(term);
	if (!term->closing)
		lds_terminal_schedule_focus_current_term(term->parent);
	lds_terminal_menu_sync_edit_actions(term->parent);
}

gboolean lds_terminal_vte_promote_secondary(LdsTerminalTerm *term, GtkWidget *old_primary) {
	if (!term || !term->secondary_vte)
		return FALSE;

	GtkWidget *new_primary = term->secondary_vte;
	GPid new_pid = term->secondary_pid;
	g_object_ref(new_primary);
	if (term->pane_split && GTK_IS_PANED(term->pane_split)) {
		lds_terminal_vte_detach_end_child(term->pane_split, new_primary, FALSE);
		gtk_paned_set_start_child(GTK_PANED(term->pane_split), new_primary);
	}

	g_signal_handlers_disconnect_by_data(new_primary, term);
	term->vte = new_primary;
	term->pid = new_pid;
	lds_terminal_vte_clear_secondary_state(term);
	lds_terminal_vte_rebuild_single_pane_container(term);
	lds_terminal_vte_attach_controllers(term, new_primary, FALSE);
	lds_terminal_vte_refresh_tab_label(term);
	lds_terminal_schedule_focus_current_term(term->parent);
	(void)old_primary;
	g_object_unref(new_primary);
	lds_terminal_menu_sync_edit_actions(term->parent);
	return TRUE;
}
