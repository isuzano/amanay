/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Link span detection and normalization.
 */

#include <glib.h>
#include <string.h>

#include "internal/link_detect.h"

typedef struct {
	guint start_col;
	guint end_col;
	LdsLinkKind kind;
	gchar *target;
	guint priority;
} LdsLinkCandidate;

static const gchar *const lds_tld_composed[] = {
	"com.br", "org.br", "net.br", "gov.br", "edu.br", "co.uk",	"org.uk", "ac.uk",	"com.au",
	"net.au", "org.au", "co.jp",  "ne.jp",	"com.mx", "com.ar", "com.co", "com.pe", NULL};

static const gchar *const lds_tld_simple[] = {
	"com", "org", "net",  "io",		"dev",	 "app",	  "tech", "ai", "co", "me", "info",
	"biz", "xyz", "site", "online", "cloud", "store", "blog", "br", "us", "uk", "de",
	"fr",  "es",  "it",	  "pt",		"nl",	 "ru",	  "jp",	  "kr", "cn", "ca", "au",
	"ch",  "se",  "no",	  "fi",		"dk",	 "be",	  "pl",	  NULL};

static const gchar *const lds_tld_ext_blacklist[] = {
	"c",	 "h",  "cpp", "hpp",  "rs",	  "go",	  "py",	 "js",	 "ts",	"java", "kt",
	"swift", "rb", "php", "json", "xml",  "yaml", "yml", "toml", "ini", "conf", "log",
	"txt",	 "md", "png", "jpg",  "jpeg", "gif",  "svg", "pdf",	 "zip", "tar",	"gz",
	"bz2",	 "xz", "7z",  "so",	  "dll",  "exe",  "bin", NULL};

static gboolean lds_is_word_char(char c) {
	return g_ascii_isalnum(c) || c == '_';
}

static guint lds_utf8_col_from_byte(const gchar *line, guint byte_idx) {
	if (!line)
		return byte_idx;
	if (g_utf8_validate(line, -1, NULL))
		return (guint)g_utf8_pointer_to_offset(line, line + byte_idx);
	return byte_idx;
}

static gboolean lds_has_scheme(const gchar *s) {
	return g_ascii_strncasecmp(s, "http://", 7) == 0 || g_ascii_strncasecmp(s, "https://", 8) == 0;
}

static gboolean lds_is_all_digits_dots(const gchar *s) {
	gboolean saw_digit = FALSE;
	for (const gchar *p = s; *p; p++) {
		if (g_ascii_isdigit(*p)) {
			saw_digit = TRUE;
			continue;
		}
		if (*p != '.')
			return FALSE;
	}
	return saw_digit;
}

static gboolean lds_is_version_like(const gchar *s) {
	gboolean saw_dot = FALSE;
	for (const gchar *p = s; *p; p++) {
		if (g_ascii_isdigit(*p))
			continue;
		if (*p == '.') {
			saw_dot = TRUE;
			continue;
		}
		return FALSE;
	}
	return saw_dot;
}

static gboolean lds_tld_in_set(const gchar *tld, const gchar *const *set) {
	for (gsize i = 0; set[i] != NULL; i++) {
		if (g_ascii_strcasecmp(tld, set[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean lds_domain_tld_is_curated(const gchar *domain) {
	g_autofree gchar *lower = g_ascii_strdown(domain, -1);
	for (gsize i = 0; lds_tld_composed[i] != NULL; i++) {
		const gchar *suffix = lds_tld_composed[i];
		gsize dlen = strlen(lower);
		gsize slen = strlen(suffix);
		if (dlen > slen && g_ascii_strcasecmp(lower + (dlen - slen), suffix) == 0 &&
			lower[dlen - slen - 1] == '.')
			return TRUE;
	}

	const gchar *dot = strrchr(lower, '.');
	if (!dot || !*(dot + 1))
		return FALSE;
	return lds_tld_in_set(dot + 1, lds_tld_simple);
}

static gboolean lds_validate_domain_labels(const gchar *domain) {
	g_auto(GStrv) labels = g_strsplit(domain, ".", -1);
	if (!labels || !labels[0] || !labels[1])
		return FALSE;

	for (gsize i = 0; labels[i] != NULL; i++) {
		const gchar *label = labels[i];
		gsize len = strlen(label);
		if (len == 0 || len > 63)
			return FALSE;
		if (label[0] == '-' || label[len - 1] == '-')
			return FALSE;
		for (gsize j = 0; j < len; j++) {
			char c = label[j];
			if (!(g_ascii_isalnum(c) || c == '-'))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean lds_validate_domain_tld(const gchar *domain) {
	g_autofree gchar *lower = g_ascii_strdown(domain, -1);
	for (gsize i = 0; lds_tld_composed[i] != NULL; i++) {
		const gchar *suffix = lds_tld_composed[i];
		gsize dlen = strlen(lower);
		gsize slen = strlen(suffix);
		if (dlen > slen && g_ascii_strcasecmp(lower + (dlen - slen), suffix) == 0 &&
			lower[dlen - slen - 1] == '.')
			return TRUE;
	}

	const gchar *last_dot = strrchr(lower, '.');
	if (!last_dot || !*(last_dot + 1))
		return FALSE;

	const gchar *tld = last_dot + 1;
	if (lds_tld_in_set(tld, lds_tld_ext_blacklist))
		return FALSE;
	if (lds_tld_in_set(tld, lds_tld_simple))
		return TRUE;

	gsize len = strlen(tld);
	if (len >= 2) {
		for (gsize i = 0; i < len; i++) {
			if (!g_ascii_isalpha(tld[i]))
				return FALSE;
		}
		return TRUE;
	}

	return FALSE;
}

static gboolean lds_trim_trailing_punct(const gchar *line, guint *start, guint *end) {
	if (!line || !start || !end || *end <= *start)
		return FALSE;

	while (*end > *start) {
		char c = line[*end - 1];
		gboolean trim = FALSE;
		switch (c) {
		case ',':
		case '.':
		case ':':
		case ';':
		case '!':
		case '?':
			trim = TRUE;
			break;
		case ')':
		case ']':
		case '}': {
			char open = c == ')' ? '(' : (c == ']' ? '[' : '{');
			guint opens = 0, closes = 0;
			for (guint i = *start; i < *end; i++) {
				if (line[i] == open)
					opens++;
				else if (line[i] == c)
					closes++;
			}
			if (closes > opens)
				trim = TRUE;
			break;
		}
		default:
			break;
		}
		if (!trim)
			break;
		(*end)--;
	}
	return *end > *start;
}

static gboolean lds_trim_leading_punct(const gchar *line, guint *start, guint *end) {
	if (!line || !start || !end || *end <= *start)
		return FALSE;
	while (*start < *end) {
		char c = line[*start];
		if (strchr("([<{\"'`", c) == NULL)
			break;
		(*start)++;
	}
	return *end > *start;
}

static gchar *lds_substr(const gchar *line, guint start, guint end) {
	if (!line || end <= start)
		return NULL;
	return g_strndup(line + start, end - start);
}

static gboolean lds_validate_email_local(const gchar *local) {
	if (!local || !*local)
		return FALSE;
	gsize len = strlen(local);
	if (local[0] == '.' || local[len - 1] == '.')
		return FALSE;
	for (gsize i = 0; i < len; i++) {
		char c = local[i];
		if (!(g_ascii_isalnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'))
			return FALSE;
		if (c == '.' && i + 1 < len && local[i + 1] == '.')
			return FALSE;
	}
	return TRUE;
}

static LdsLinkCandidate *lds_candidate_new(guint start, guint end, LdsLinkKind kind, gchar *target,
										   guint priority) {
	LdsLinkCandidate *c = g_new0(LdsLinkCandidate, 1);
	c->start_col = start;
	c->end_col = end;
	c->kind = kind;
	c->target = target;
	c->priority = priority;
	return c;
}

static void lds_candidate_free(gpointer data) {
	LdsLinkCandidate *c = data;
	if (!c)
		return;
	g_free(c->target);
	g_free(c);
}

static gint lds_candidate_cmp(gconstpointer a, gconstpointer b) {
	const LdsLinkCandidate *ca = *(const LdsLinkCandidate *const *)a;
	const LdsLinkCandidate *cb = *(const LdsLinkCandidate *const *)b;
	if (ca->priority != cb->priority)
		return ca->priority < cb->priority ? 1 : -1;

	guint la = ca->end_col - ca->start_col;
	guint lb = cb->end_col - cb->start_col;
	if (la != lb)
		return la < lb ? 1 : -1;

	if (ca->start_col != cb->start_col)
		return ca->start_col < cb->start_col ? -1 : 1;

	return 0;
}

static gint lds_span_start_cmp(gconstpointer a, gconstpointer b) {
	const LdsLinkSpan *sa = a;
	const LdsLinkSpan *sb = b;
	if (sa->start_col < sb->start_col)
		return -1;
	if (sa->start_col > sb->start_col)
		return 1;
	return 0;
}

static gboolean lds_overlap(guint a0, guint a1, guint b0, guint b1) {
	return a0 < b1 && b0 < a1;
}

static gboolean lds_domain_has_intent_signal(const gchar *token) {
	if (!token || !*token)
		return FALSE;
	if (g_ascii_strncasecmp(token, "www.", 4) == 0)
		return TRUE;
	return strchr(token, '/') || strchr(token, '?') || strchr(token, '#') || strchr(token, ':');
}

static gboolean lds_try_url(const gchar *line, guint start, guint end, LdsLinkCandidate **out) {
	if (!line || !out || end <= start)
		return FALSE;
	guint s = start, e = end;
	if (!lds_trim_leading_punct(line, &s, &e))
		return FALSE;
	if (!lds_trim_trailing_punct(line, &s, &e))
		return FALSE;
	g_autofree gchar *token = lds_substr(line, s, e);
	if (!token || !lds_has_scheme(token))
		return FALSE;

	g_autoptr(GError) error = NULL;
	GUri *parsed = g_uri_parse(token, G_URI_FLAGS_NONE, &error);
	if (!parsed)
		return FALSE;
	g_uri_unref(parsed);

	*out = lds_candidate_new(lds_utf8_col_from_byte(line, s), lds_utf8_col_from_byte(line, e),
							 LDS_LINK_KIND_URL, g_strdup(token), 300u);
	return TRUE;
}

static gboolean lds_try_email(const gchar *line, guint start, guint end, LdsLinkCandidate **out) {
	if (!line || !out || end <= start)
		return FALSE;
	guint s = start, e = end;
	if (!lds_trim_leading_punct(line, &s, &e))
		return FALSE;
	if (!lds_trim_trailing_punct(line, &s, &e))
		return FALSE;
	g_autofree gchar *token = lds_substr(line, s, e);
	if (!token)
		return FALSE;

	char *at = strchr(token, '@');
	if (!at || strchr(at + 1, '@'))
		return FALSE;
	*at = '\0';
	const gchar *local = token;
	const gchar *domain = at + 1;
	if (!lds_validate_email_local(local))
		return FALSE;
	if (!lds_validate_domain_labels(domain))
		return FALSE;
	if (!lds_validate_domain_tld(domain))
		return FALSE;

	g_autofree gchar *full = g_strdup_printf("%s@%s", local, domain);
	*out = lds_candidate_new(lds_utf8_col_from_byte(line, s), lds_utf8_col_from_byte(line, e),
							 LDS_LINK_KIND_EMAIL, g_strdup_printf("mailto:%s", full), 200u);
	return TRUE;
}

static gboolean lds_try_domain(const gchar *line, guint start, guint end, LdsLinkCandidate **out) {
	if (!line || !out || end <= start)
		return FALSE;

	guint s = start, e = end;
	if (!lds_trim_leading_punct(line, &s, &e))
		return FALSE;
	if (!lds_trim_trailing_punct(line, &s, &e))
		return FALSE;
	g_autofree gchar *token = lds_substr(line, s, e);
	if (!token || !strchr(token, '.'))
		return FALSE;
	if (strchr(token, '@'))
		return FALSE;
	if (lds_has_scheme(token))
		return FALSE;
	if (strchr(token, '_'))
		return FALSE;

	gsize host_len = strcspn(token, "/?#:");
	if (host_len == 0)
		return FALSE;
	g_autofree gchar *host = g_strndup(token, host_len);
	const gchar *tail = token + host_len;

	if (*tail == ':') {
		const gchar *p = tail + 1;
		if (!g_ascii_isdigit(*p))
			return FALSE;
		while (g_ascii_isdigit(*p))
			p++;
		if (*p != '\0' && *p != '/' && *p != '?' && *p != '#')
			return FALSE;
	}

	if (lds_is_all_digits_dots(host) || lds_is_version_like(host))
		return FALSE;
	if (!lds_validate_domain_labels(host))
		return FALSE;
	if (!lds_validate_domain_tld(host))
		return FALSE;
	if (!lds_domain_tld_is_curated(host) && !lds_domain_has_intent_signal(token) &&
		g_ascii_strncasecmp(token, "www.", 4) != 0)
		return FALSE;
	if (!lds_domain_has_intent_signal(token)) {
		char prev = s > 0 ? line[s - 1] : ' ';
		char next = line[e];
		if (lds_is_word_char(prev) || lds_is_word_char(next))
			return FALSE;
	}

	*out = lds_candidate_new(lds_utf8_col_from_byte(line, s), lds_utf8_col_from_byte(line, e),
							 LDS_LINK_KIND_DOMAIN, g_strdup_printf("https://%s", token), 100u);
	return TRUE;
}

void lds_link_span_free(gpointer data) {
	LdsLinkSpan *span = data;
	if (!span)
		return;
	g_free(span->target);
	g_free(span);
}

GPtrArray *lds_link_detect_line(const gchar *line_utf8, const LdsOsc8Span *osc8_spans,
								gsize osc8_len) {
	GPtrArray *candidates = g_ptr_array_new_with_free_func(lds_candidate_free);
	GPtrArray *out = g_ptr_array_new_with_free_func(lds_link_span_free);

	if (!line_utf8 || !*line_utf8)
		return out;

	gsize line_len = strlen(line_utf8);

	for (gsize i = 0; i < osc8_len; i++) {
		const LdsOsc8Span *s = &osc8_spans[i];
		if (!s->uri || s->end_col <= s->start_col)
			continue;
		guint start = s->start_col;
		guint end = s->end_col;
		if (start >= line_len)
			continue;
		if (end > line_len)
			end = (guint)line_len;
		if (end <= start)
			continue;
		g_ptr_array_add(candidates,
						lds_candidate_new(start, end, LDS_LINK_KIND_OSC8, g_strdup(s->uri), 400u));
	}

	guint i = 0;
	while (i < line_len) {
		while (i < line_len && g_ascii_isspace(line_utf8[i]))
			i++;
		if (i >= line_len)
			break;
		guint start = i;
		while (i < line_len && !g_ascii_isspace(line_utf8[i]))
			i++;
		guint end = i;

		LdsLinkCandidate *cand = NULL;
		if (lds_try_url(line_utf8, start, end, &cand)) {
			g_ptr_array_add(candidates, cand);
			continue;
		}
		if (lds_try_email(line_utf8, start, end, &cand)) {
			g_ptr_array_add(candidates, cand);
			continue;
		}
		if (lds_try_domain(line_utf8, start, end, &cand))
			g_ptr_array_add(candidates, cand);
	}

	g_ptr_array_sort(candidates, lds_candidate_cmp);
	for (guint cidx = 0; cidx < candidates->len; cidx++) {
		LdsLinkCandidate *c = g_ptr_array_index(candidates, cidx);
		gboolean conflict = FALSE;
		for (guint aidx = 0; aidx < out->len; aidx++) {
			LdsLinkSpan *a = g_ptr_array_index(out, aidx);
			if (lds_overlap(c->start_col, c->end_col, a->start_col, a->end_col)) {
				conflict = TRUE;
				break;
			}
		}
		if (conflict)
			continue;

		LdsLinkSpan *span = g_new0(LdsLinkSpan, 1);
		span->start_col = c->start_col;
		span->end_col = c->end_col;
		span->kind = c->kind;
		span->target = g_strdup(c->target);
		span->flags = 0;
		g_ptr_array_add(out, span);
	}

	g_ptr_array_sort(out, lds_span_start_cmp);
	g_ptr_array_unref(candidates);
	return out;
}
