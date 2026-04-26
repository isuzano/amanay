/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Link detection behavior tests.
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "internal/link_detect.h"

static void test_link_detect_url_trim(void) {
	g_autoptr(GPtrArray) spans = lds_link_detect_line("open https://example.com). now", NULL, 0);
	g_assert_cmpuint(spans->len, ==, 1);
	LdsLinkSpan *s = g_ptr_array_index(spans, 0);
	g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_URL);
	g_assert_cmpstr(s->target, ==, "https://example.com");
}

static void test_link_detect_email(void) {
	g_autoptr(GPtrArray) spans =
		lds_link_detect_line("mail user.name+tag@sub.domain.com ok", NULL, 0);
	g_assert_cmpuint(spans->len, ==, 1);
	LdsLinkSpan *s = g_ptr_array_index(spans, 0);
	g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_EMAIL);
	g_assert_cmpstr(s->target, ==, "mailto:user.name+tag@sub.domain.com");
}

static void test_link_detect_domain_accepts_real_world(void) {
	g_autoptr(GPtrArray) spans = lds_link_detect_line("go to netkings.com.br/path", NULL, 0);
	g_assert_cmpuint(spans->len, ==, 1);
	LdsLinkSpan *s = g_ptr_array_index(spans, 0);
	g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_DOMAIN);
	g_assert_cmpstr(s->target, ==, "https://netkings.com.br/path");
}

static void test_link_detect_domain_rejects_file_ext_and_version(void) {
	g_autoptr(GPtrArray) spans =
		lds_link_detect_line("main.c v1.2.3 config.yaml image.png", NULL, 0);
	g_assert_cmpuint(spans->len, ==, 0);
}

static void test_link_detect_osc8_priority(void) {
	LdsOsc8Span osc8[] = {{.start_col = 5, .end_col = 22, .uri = "https://from-osc8.example"}};
	g_autoptr(GPtrArray) spans = lds_link_detect_line("see https://example.com now", osc8, 1);
	g_assert_cmpuint(spans->len, ==, 1);
	LdsLinkSpan *s = g_ptr_array_index(spans, 0);
	g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_OSC8);
	g_assert_cmpstr(s->target, ==, "https://from-osc8.example");
}

typedef struct {
	const gchar *input;
	gboolean expect_link;
	LdsLinkKind kind;
	const gchar *target;
} LinkCase;

static void test_link_detect_matrix(void) {
	static const LinkCase cases[] = {
		{"netkings.com.br", TRUE, LDS_LINK_KIND_DOMAIN, "https://netkings.com.br"},
		{"sub.domain.io", TRUE, LDS_LINK_KIND_DOMAIN, "https://sub.domain.io"},
		{"www.example.com", TRUE, LDS_LINK_KIND_DOMAIN, "https://www.example.com"},
		{"example.com/path?q=1", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com/path?q=1"},
		{"example.com:443/path", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com:443/path"},
		{"https://example.com", TRUE, LDS_LINK_KIND_URL, "https://example.com"},
		{"https://example.com).", TRUE, LDS_LINK_KIND_URL, "https://example.com"},
		{"user.name+tag@sub.domain.com", TRUE, LDS_LINK_KIND_EMAIL,
		 "mailto:user.name+tag@sub.domain.com"},
		{"main.c", FALSE, 0, NULL},
		{"header.hpp", FALSE, 0, NULL},
		{"script.py", FALSE, 0, NULL},
		{"package.json", FALSE, 0, NULL},
		{"doc.md", FALSE, 0, NULL},
		{"config.yaml", FALSE, 0, NULL},
		{"archive.tar", FALSE, 0, NULL},
		{"archive.tar.gz", FALSE, 0, NULL},
		{"image.png", FALSE, 0, NULL},
		{"paper.pdf", FALSE, 0, NULL},
		{"lib.so", FALSE, 0, NULL},
		{"v1.2.3", FALSE, 0, NULL},
		{"1.2.3.4", FALSE, 0, NULL},
		{"foo_bar.com", FALSE, 0, NULL},
		{"-bad.com", FALSE, 0, NULL},
		{"bad-.com", FALSE, 0, NULL},
		{"a@b", FALSE, 0, NULL},
		{"a..b@example.com", FALSE, 0, NULL},
		{"a.@example.com", FALSE, 0, NULL},
		{".a@example.com", FALSE, 0, NULL},
		{"tokenexample.comtoken", FALSE, 0, NULL},
		{"tokenexample.comtoken/path", TRUE, LDS_LINK_KIND_DOMAIN,
		 "https://tokenexample.comtoken/path"},
		{"(example.com)", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com"},
		{"example.com,", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com"},
		{"example.com;", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com"},
		{"example.com!?", TRUE, LDS_LINK_KIND_DOMAIN, "https://example.com"},
	};

	for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
		const LinkCase *c = &cases[i];
		g_autoptr(GPtrArray) spans = lds_link_detect_line(c->input, NULL, 0);
		if (!c->expect_link) {
			g_assert_cmpuint(spans->len, ==, 0);
			continue;
		}

		g_assert_cmpuint(spans->len, >=, 1);
		LdsLinkSpan *s = g_ptr_array_index(spans, 0);
		g_assert_cmpint(s->kind, ==, c->kind);
		g_assert_cmpstr(s->target, ==, c->target);
	}
}

static void test_link_detect_stress_10k_lines(void) {
	guint detected_total = 0;
	for (guint i = 0; i < 10000; i++) {
		g_autofree gchar *line = NULL;
		switch (i % 5) {
		case 0:
			line = g_strdup_printf("build-%u main.c v1.2.3 token_%u", i, i);
			break;
		case 1:
			line = g_strdup_printf("visit https://example%u.com/path?q=%u).", i, i);
			break;
		case 2:
			line = g_strdup_printf("contact user%u.name+tag@sub.domain.com now", i);
			break;
		case 3:
			line = g_strdup_printf("domain netkings.com.br/%u and image.png", i);
			break;
		default:
			line = g_strdup_printf("(sub.domain.io) foo.bar.baz%u", i);
			break;
		}

		g_autoptr(GPtrArray) spans = lds_link_detect_line(line, NULL, 0);
		detected_total += spans->len;
	}

	/* Must detect many links but not crash or explode on noisy inputs. */
	g_assert_cmpuint(detected_total, >=, 5000);
}

static void test_link_detect_utf8_column_offsets(void) {
	/* Prefix has 3 UTF-8 chars but 5 bytes. Domain should start at col 3. */
	const gchar *line = "áá netkings.com.br";
	g_autoptr(GPtrArray) spans = lds_link_detect_line(line, NULL, 0);
	g_assert_cmpuint(spans->len, ==, 1);
	LdsLinkSpan *s = g_ptr_array_index(spans, 0);
	g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_DOMAIN);
	g_assert_cmpuint(s->start_col, ==, 3u);
	g_assert_cmpuint(s->end_col, ==, 18u);
}

static void test_link_detect_generated_golden_100_plus(void) {
	guint checked = 0;

	for (guint i = 0; i < 50; i++) {
		g_autofree gchar *line = g_strdup_printf("case %u: sub%u.domain.io/path", i, i);
		g_autoptr(GPtrArray) spans = lds_link_detect_line(line, NULL, 0);
		g_assert_cmpuint(spans->len, >=, 1);
		LdsLinkSpan *s = g_ptr_array_index(spans, 0);
		g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_DOMAIN);
		checked++;
	}

	for (guint i = 0; i < 30; i++) {
		g_autofree gchar *line = g_strdup_printf("mail u%u.name+tag@sub.domain.com", i);
		g_autoptr(GPtrArray) spans = lds_link_detect_line(line, NULL, 0);
		g_assert_cmpuint(spans->len, >=, 1);
		LdsLinkSpan *s = g_ptr_array_index(spans, 0);
		g_assert_cmpint(s->kind, ==, LDS_LINK_KIND_EMAIL);
		checked++;
	}

	for (guint i = 0; i < 30; i++) {
		g_autofree gchar *line = g_strdup_printf("noise%u main.c v1.2.%u image.png", i, i);
		g_autoptr(GPtrArray) spans = lds_link_detect_line(line, NULL, 0);
		g_assert_cmpuint(spans->len, ==, 0);
		checked++;
	}

	g_assert_cmpuint(checked, >=, 110);
}

static LdsLinkKind test_kind_from_string(const char *kind) {
	if (g_strcmp0(kind, "OSC8") == 0)
		return LDS_LINK_KIND_OSC8;
	if (g_strcmp0(kind, "URL") == 0)
		return LDS_LINK_KIND_URL;
	if (g_strcmp0(kind, "EMAIL") == 0)
		return LDS_LINK_KIND_EMAIL;
	return LDS_LINK_KIND_DOMAIN;
}

static void test_link_detect_golden_json_file(void) {
#ifndef LDS_LINK_DETECT_CASES_JSON_PATH
#error "LDS_LINK_DETECT_CASES_JSON_PATH must be defined by build"
#endif

	g_autoptr(GError) error = NULL;
	JsonParser *parser = json_parser_new();
	gboolean ok = json_parser_load_from_file(parser, LDS_LINK_DETECT_CASES_JSON_PATH, &error);
	g_assert_true(ok);
	g_assert_no_error(error);

	JsonNode *root = json_parser_get_root(parser);
	g_assert_true(JSON_NODE_HOLDS_ARRAY(root));
	JsonArray *cases = json_node_get_array(root);
	g_assert_nonnull(cases);
	g_assert_cmpuint(json_array_get_length(cases), >=, 10);

	for (guint i = 0; i < json_array_get_length(cases); i++) {
		JsonObject *obj = json_array_get_object_element(cases, i);
		g_assert_nonnull(obj);
		const char *input = json_object_get_string_member(obj, "input");
		g_assert_nonnull(input);

		g_autoptr(GPtrArray) spans = lds_link_detect_line(input, NULL, 0);

		JsonNode *expect_node = json_object_get_member(obj, "expect");
		if (!expect_node || JSON_NODE_HOLDS_NULL(expect_node)) {
			g_assert_cmpuint(spans->len, ==, 0);
			continue;
		}

		g_assert_true(JSON_NODE_HOLDS_OBJECT(expect_node));
		JsonObject *expect = json_node_get_object(expect_node);
		const char *kind = json_object_get_string_member(expect, "kind");
		const char *target = json_object_get_string_member(expect, "target");
		g_assert_nonnull(kind);
		g_assert_nonnull(target);

		g_assert_cmpuint(spans->len, >=, 1);
		LdsLinkSpan *s = g_ptr_array_index(spans, 0);
		g_assert_cmpint(s->kind, ==, test_kind_from_string(kind));
		g_assert_cmpstr(s->target, ==, target);
	}

	g_object_unref(parser);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/link-detect/url-trim", test_link_detect_url_trim);
	g_test_add_func("/link-detect/email", test_link_detect_email);
	g_test_add_func("/link-detect/domain-real-world", test_link_detect_domain_accepts_real_world);
	g_test_add_func("/link-detect/domain-reject-file-ext-version",
					test_link_detect_domain_rejects_file_ext_and_version);
	g_test_add_func("/link-detect/osc8-priority", test_link_detect_osc8_priority);
	g_test_add_func("/link-detect/matrix", test_link_detect_matrix);
	g_test_add_func("/link-detect/stress-10k-lines", test_link_detect_stress_10k_lines);
	g_test_add_func("/link-detect/utf8-column-offsets", test_link_detect_utf8_column_offsets);
	g_test_add_func("/link-detect/generated-golden-100-plus",
					test_link_detect_generated_golden_100_plus);
	g_test_add_func("/link-detect/golden-json-file", test_link_detect_golden_json_file);

	return g_test_run();
}
