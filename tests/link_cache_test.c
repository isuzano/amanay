/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Link cache behavior tests.
 */

#include <glib.h>

#include "internal/link_cache.h"

static void test_link_cache_hit_miss_generation(void) {
	LdsLinkLineCache *cache = lds_link_line_cache_new(8);
	gboolean recomputed = FALSE;

	const GPtrArray *a =
		lds_link_line_cache_get_or_detect(cache, 10, 1, "netkings.com.br", NULL, 0, &recomputed);
	g_assert_nonnull(a);
	g_assert_true(recomputed);
	g_assert_cmpuint(a->len, ==, 1);

	const GPtrArray *b =
		lds_link_line_cache_get_or_detect(cache, 10, 1, "netkings.com.br", NULL, 0, &recomputed);
	g_assert_nonnull(b);
	g_assert_false(recomputed);
	g_assert_true(a == b);

	const GPtrArray *c =
		lds_link_line_cache_get_or_detect(cache, 10, 2, "netkings.com.br", NULL, 0, &recomputed);
	g_assert_nonnull(c);
	g_assert_true(recomputed);
	g_assert_cmpuint(c->len, ==, 1);

	lds_link_line_cache_free(cache);
}

static void test_link_cache_invalidate_range(void) {
	LdsLinkLineCache *cache = lds_link_line_cache_new(8);
	gboolean recomputed = FALSE;

	(void)lds_link_line_cache_get_or_detect(cache, 1, 1, "a.com", NULL, 0, &recomputed);
	(void)lds_link_line_cache_get_or_detect(cache, 2, 1, "b.com", NULL, 0, &recomputed);
	(void)lds_link_line_cache_get_or_detect(cache, 3, 1, "c.com", NULL, 0, &recomputed);
	g_assert_cmpuint(lds_link_line_cache_size(cache), ==, 3);

	lds_link_line_cache_invalidate_range(cache, 2, 3);
	g_assert_cmpuint(lds_link_line_cache_size(cache), ==, 1);

	lds_link_line_cache_free(cache);
}

static void test_link_cache_lru_bound(void) {
	LdsLinkLineCache *cache = lds_link_line_cache_new(2);
	gboolean recomputed = FALSE;

	(void)lds_link_line_cache_get_or_detect(cache, 1, 1, "a.com", NULL, 0, &recomputed);
	(void)lds_link_line_cache_get_or_detect(cache, 2, 1, "b.com", NULL, 0, &recomputed);
	g_assert_cmpuint(lds_link_line_cache_size(cache), ==, 2);
	(void)lds_link_line_cache_get_or_detect(cache, 3, 1, "c.com", NULL, 0, &recomputed);
	g_assert_cmpuint(lds_link_line_cache_size(cache), ==, 2);

	/* row 1 should have been evicted as the LRU */
	(void)lds_link_line_cache_get_or_detect(cache, 1, 1, "a.com", NULL, 0, &recomputed);
	g_assert_true(recomputed);

	lds_link_line_cache_free(cache);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/link-cache/hit-miss-generation", test_link_cache_hit_miss_generation);
	g_test_add_func("/link-cache/invalidate-range", test_link_cache_invalidate_range);
	g_test_add_func("/link-cache/lru-bound", test_link_cache_lru_bound);
	return g_test_run();
}
