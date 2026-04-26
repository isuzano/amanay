/**
 * SPDX-FileCopyrightText: 2025-2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Command-line argument parsing and validation.
 */

#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib.h>

#include "lds_terminal.h"
#include "internal/app_args.h"
#include "internal/lds_terminal_internal.h"

#ifndef GDK_HINT_POS
#define GDK_HINT_POS 0x02
#endif

#ifndef GDK_HINT_SIZE
#define GDK_HINT_SIZE 0x04
#endif

#ifndef LDS_TERMINAL_DEFAULT_RENDERER_MODE
#define LDS_TERMINAL_DEFAULT_RENDERER_MODE LDS_TERMINAL_RENDERER_AUTO
#endif

static int lds_terminal_parse_geometry(const gchar *spec, int *x, int *y, int *width, int *height);

gboolean lds_terminal_parse_args(LdsTerminalState *state, int *argc, char ***argv, GError **error) {
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GError) local_error = NULL;
	gchar *geometry = NULL;
	gchar *command_line = NULL;
	gchar *renderer = NULL;
	gboolean force_truecolor = FALSE;
	gboolean disable_truecolor = FALSE;

	if (!state)
		return FALSE;

	state->args = (LdsTerminalCommandArgs){0};
	state->args.truecolor_mode = LDS_TERMINAL_TRUECOLOR_AUTO;
	state->args.renderer_mode = LDS_TERMINAL_DEFAULT_RENDERER_MODE;
	state->args.compat_local_services = FALSE;

	context = g_option_context_new(NULL);
	g_option_context_set_summary(context, "Amanay options");

	state->args.executable = g_strdup((*argv)[0]);

	GOptionEntry entries[] = {
		{"working-directory", 'w', 0, G_OPTION_ARG_FILENAME, &state->args.working_directory,
		 "Set working directory", "DIR"},
		{"title", 't', 0, G_OPTION_ARG_STRING, &state->args.title, "Set initial window title",
		 "TITLE"},
		{"tabs", 0, 0, G_OPTION_ARG_STRING, &state->args.tabs, "Tabs specification", "TABS"},
		{"profile", 'p', 0, G_OPTION_ARG_STRING, &state->args.profile, "Profile name", "PROFILE"},
		{"login", 'l', 0, G_OPTION_ARG_NONE, &state->args.login_shell,
		 "Run command as a login shell", NULL},
		{"no-remote", 0, 0, G_OPTION_ARG_NONE, &state->args.no_remote, "Disable remote control",
		 NULL},
		{"compat-local-services", 0, 0, G_OPTION_ARG_NONE, &state->args.compat_local_services,
		 "Force local VFS and disable desktop portals", NULL},
		{"truecolor", 0, 0, G_OPTION_ARG_NONE, &force_truecolor,
		 "Force COLORTERM=truecolor and TERM=xterm-256color", NULL},
		{"no-truecolor", 0, 0, G_OPTION_ARG_NONE, &disable_truecolor,
		 "Disable TrueColor environment injection", NULL},
		{"renderer", 0, 0, G_OPTION_ARG_STRING, &renderer, "GSK renderer: auto|cairo|ngl|vulkan",
		 "MODE"},
		{"geometry", 'g', 0, G_OPTION_ARG_STRING, &geometry, "Initial geometry (COLSxROWS+X+Y)",
		 "GEOMETRY"},
		{"command", 'e', 0, G_OPTION_ARG_STRING, &command_line, "Command to execute", "COMMAND"},
		{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &state->args.command,
		 "Command to execute", "ARGS"},
		{NULL}};

	g_option_context_add_main_entries(context, entries, NULL);

	if (!g_option_context_parse(context, argc, argv, &local_error)) {
		g_propagate_error(error, g_steal_pointer(&local_error));
		return FALSE;
	}

	if (command_line && state->args.command) {
		g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
							"both --command and positional arguments were provided");
		return FALSE;
	}

	if (force_truecolor && disable_truecolor) {
		g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
							"both --truecolor and --no-truecolor were provided");
		return FALSE;
	}

	if (force_truecolor)
		state->args.truecolor_mode = LDS_TERMINAL_TRUECOLOR_FORCE;
	else if (disable_truecolor)
		state->args.truecolor_mode = LDS_TERMINAL_TRUECOLOR_DISABLE;

	if (renderer) {
		if (g_strcmp0(renderer, "auto") == 0)
			state->args.renderer_mode = LDS_TERMINAL_RENDERER_AUTO;
		else if (g_strcmp0(renderer, "cairo") == 0)
			state->args.renderer_mode = LDS_TERMINAL_RENDERER_CAIRO;
		else if (g_strcmp0(renderer, "ngl") == 0)
			state->args.renderer_mode = LDS_TERMINAL_RENDERER_NGL;
		else if (g_strcmp0(renderer, "vulkan") == 0)
			state->args.renderer_mode = LDS_TERMINAL_RENDERER_VULKAN;
		else {
			g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
						"invalid --renderer value: '%s' (expected auto|cairo|ngl|vulkan)",
						renderer);
			return FALSE;
		}
	}

	if (command_line) {
		if (!g_shell_parse_argv(command_line, NULL, &state->args.command, &local_error)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
	}

	if (geometry) {
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		int mask = lds_terminal_parse_geometry(geometry, &x, &y, &width, &height);
		if (mask < 0) {
			g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
						"invalid --geometry value: '%s'", geometry);
			return FALSE;
		}

		state->args.geometry_bitmask = mask;

		if (mask & GDK_HINT_SIZE) {
			if (width > 0)
				state->args.geometry_columns = (guint)width;

			if (height > 0)
				state->args.geometry_rows = (guint)height;
		}

		if (mask & GDK_HINT_POS) {
			state->args.geometry_xoff = x;
			state->args.geometry_yoff = y;
		}
	}

	return TRUE;
}

const char *lds_terminal_renderer_env_value(gint mode) {
	switch (mode) {
	case LDS_TERMINAL_RENDERER_CAIRO:
		return "cairo";
	case LDS_TERMINAL_RENDERER_NGL:
		return "ngl";
	case LDS_TERMINAL_RENDERER_VULKAN:
		return "vulkan";
	case LDS_TERMINAL_RENDERER_AUTO:
	default:
		return NULL;
	}
}

static int lds_terminal_parse_geometry(const gchar *spec, int *x, int *y, int *width, int *height) {
	if (!spec || !*spec)
		return 0;

	const gchar *p = spec;
	gchar *end = NULL;
	int mask = 0;
	gboolean parsed_any = FALSE;

	if (g_ascii_isdigit(*p)) {
		long w = g_ascii_strtoll(p, &end, 10);
		if (end == p || (*end != 'x' && *end != 'X'))
			return -1;

		p = end + 1;
		long h = g_ascii_strtoll(p, &end, 10);
		if (end == p)
			return -1;
		if (w <= 0 || h <= 0 || w > G_MAXINT || h > G_MAXINT)
			return -1;

		if (width)
			*width = (int)w;
		if (height)
			*height = (int)h;
		mask |= GDK_HINT_SIZE;
		parsed_any = TRUE;
		p = end;
	}

	if (*p == '+' || *p == '-') {
		long xv = g_ascii_strtoll(p, &end, 10);
		if (end == p || xv < G_MININT || xv > G_MAXINT)
			return -1;
		if (x)
			*x = (int)xv;
		mask |= GDK_HINT_POS;
		parsed_any = TRUE;
		p = end;
	}

	if (*p == '+' || *p == '-') {
		long yv = g_ascii_strtoll(p, &end, 10);
		if (end == p || yv < G_MININT || yv > G_MAXINT)
			return -1;
		if (y)
			*y = (int)yv;
		mask |= GDK_HINT_POS;
		parsed_any = TRUE;
		p = end;
	}

	if (*p != '\0' || !parsed_any)
		return -1;

	return mask;
}
