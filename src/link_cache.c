/**
 * SPDX-FileCopyrightText: 2026 Iuri Suzano <iuri@astware.bar>
 * SPDX-License-Identifier: MIT
 *
 * Line-level link cache management.
 */

#include <glib.h>

#include "internal/link_cache.h"

typedef struct {
	gint row;
	guint64 generation;
	guint64 text_hash;
	guint64 last_used_tick;
	GPtrArray *links;
} LdsLinkLineCacheEntry;

struct _LdsLinkLineCache {
	GHashTable *entries;
	guint max_entries;
	guint64 tick;
};

static guint64 lds_link_cache_hash_fnv1a(const gchar *text) {
	const guint64 offset_basis = G_GUINT64_CONSTANT(14695981039346656037);
	const guint64 prime = G_GUINT64_CONSTANT(1099511628211);
	guint64 h = offset_basis;
	if (!text)
		return h;
	for (const guchar *p = (const guchar *)text; *p; p++) {
		h ^= (guint64)(*p);
		h *= prime;
	}
	return h;
}

static void lds_link_line_cache_entry_free(gpointer data) {
	LdsLinkLineCacheEntry *entry = data;
	if (!entry)
		return;
	if (entry->links)
		g_ptr_array_unref(entry->links);
	g_free(entry);
}

static GPtrArray *lds_link_span_array_dup(const GPtrArray *src) {
	GPtrArray *dst = g_ptr_array_new_with_free_func(lds_link_span_free);
	if (!src)
		return dst;
	for (guint i = 0; i < src->len; i++) {
		const LdsLinkSpan *s = g_ptr_array_index((GPtrArray *)src, i);
		if (!s)
			continue;
		LdsLinkSpan *c = g_new0(LdsLinkSpan, 1);
		c->start_col = s->start_col;
		c->end_col = s->end_col;
		c->kind = s->kind;
		c->flags = s->flags;
		c->target = g_strdup(s->target);
		g_ptr_array_add(dst, c);
	}
	return dst;
}

static void lds_link_line_cache_evict_if_needed(LdsLinkLineCache *cache) {
	if (!cache || cache->max_entries == 0)
		return;

	while (g_hash_table_size(cache->entries) > cache->max_entries) {
		GHashTableIter it;
		gpointer key = NULL;
		gpointer value = NULL;
		gpointer victim_key = NULL;
		LdsLinkLineCacheEntry *victim = NULL;
		g_hash_table_iter_init(&it, cache->entries);
		while (g_hash_table_iter_next(&it, &key, &value)) {
			LdsLinkLineCacheEntry *entry = value;
			if (!victim || entry->last_used_tick < victim->last_used_tick) {
				victim = entry;
				victim_key = key;
			}
		}
		if (!victim_key)
			break;
		g_hash_table_remove(cache->entries, victim_key);
	}
}

LdsLinkLineCache *lds_link_line_cache_new(guint max_entries) {
	LdsLinkLineCache *cache = g_new0(LdsLinkLineCache, 1);
	cache->max_entries = max_entries > 0 ? max_entries : 512u;
	cache->entries =
		g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, lds_link_line_cache_entry_free);
	return cache;
}

void lds_link_line_cache_free(LdsLinkLineCache *cache) {
	if (!cache)
		return;
	if (cache->entries)
		g_hash_table_destroy(cache->entries);
	g_free(cache);
}

void lds_link_line_cache_invalidate_all(LdsLinkLineCache *cache) {
	if (!cache || !cache->entries)
		return;
	g_hash_table_remove_all(cache->entries);
}

void lds_link_line_cache_invalidate_range(LdsLinkLineCache *cache, gint row_start, gint row_end) {
	if (!cache || !cache->entries)
		return;

	if (row_start > row_end) {
		gint tmp = row_start;
		row_start = row_end;
		row_end = tmp;
	}

	for (gint row = row_start; row <= row_end; row++)
		g_hash_table_remove(cache->entries, GINT_TO_POINTER(row));
}

guint lds_link_line_cache_size(const LdsLinkLineCache *cache) {
	if (!cache || !cache->entries)
		return 0u;
	return (guint)g_hash_table_size(cache->entries);
}

const GPtrArray *lds_link_line_cache_get_or_detect(LdsLinkLineCache *cache, gint row,
												   guint64 generation, const gchar *line_utf8,
												   const LdsOsc8Span *osc8_spans, gsize osc8_len,
												   gboolean *recomputed) {
	if (recomputed)
		*recomputed = FALSE;
	if (!cache || !cache->entries)
		return NULL;

	const guint64 text_hash = lds_link_cache_hash_fnv1a(line_utf8 ? line_utf8 : "");
	LdsLinkLineCacheEntry *entry = g_hash_table_lookup(cache->entries, GINT_TO_POINTER(row));
	if (entry && entry->generation == generation && entry->text_hash == text_hash && entry->links) {
		entry->last_used_tick = ++cache->tick;
		return entry->links;
	}

	g_autoptr(GPtrArray) detected =
		lds_link_detect_line(line_utf8 ? line_utf8 : "", osc8_spans, osc8_len);
	GPtrArray *stored = lds_link_span_array_dup(detected);

	if (!entry) {
		entry = g_new0(LdsLinkLineCacheEntry, 1);
		entry->row = row;
		g_hash_table_insert(cache->entries, GINT_TO_POINTER(row), entry);
	}

	if (entry->links)
		g_ptr_array_unref(entry->links);
	entry->generation = generation;
	entry->text_hash = text_hash;
	entry->last_used_tick = ++cache->tick;
	entry->links = stored;

	lds_link_line_cache_evict_if_needed(cache);
	if (recomputed)
		*recomputed = TRUE;

	return entry->links;
}
