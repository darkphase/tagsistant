/*
   Tagsistant (tagfs) -- reasoner.c
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

   Transform paths into queries and apply queries to file sets to
   grep files matching with queries.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "tagsistant.h"

/************************************************************************************/
/***                                                                              ***/
/*** Reasoner                                                                     ***/
/***                                                                              ***/
/************************************************************************************/

static GHashTable *tagsistant_reasoner_cache;

typedef struct {
	gchar tag[1024];
	gchar namespace[1024];
	gchar key[1024];
	gchar value[1024];
} tagsistant_tag;

/**
 * Destroy the values of the resoner cache.
 *
 * @param data pointer to GList
 */
void tagsistant_destroy_reasoner_value(gpointer data)
{
	GList *list = (GList *) data;
	g_list_free_full(list, g_free);
}

/**
 * Initialize reasoner library
 */
void tagsistant_reasoner_init()
{
	tagsistant_reasoner_cache = g_hash_table_new_full(
		g_str_hash,		/* key hashing */
		g_str_equal,	/* key comparison */
		g_free,			/* key destroy */
		tagsistant_destroy_reasoner_value);	/* value destroy (it's a GList, we must free is gchar * values) */
}

/**
 * Check if an and_node matches a flat tag or a triple tag
 *
 * @param and the and_node to match
 * @param tag the flat tag
 * @param namespace the namespace of the triple tag
 * @param key the key of the triple tag
 * @param value the value of the triple tag
 */
#define tagsistant_and_node_match(and, T) (\
	(and->tag && (g_strcmp0(and->tag, T->tag) == 0)) || \
	(and->namespace && (g_strcmp0(and->namespace, T->namespace) == 0) && \
	(g_strcmp0(and->key, T->key) == 0) && (g_strcmp0(and->value, T->value) == 0)))


/**
 * Add a reasoned tag to a node. Used by both tagsistant_add_reasoned_tag_callback()
 * and tagsistant_reasoner()
 *
 * @param _reasoning
 * @param result
 * @return
 */
static int tagsistant_add_reasoned_tag(tagsistant_tag *T, tagsistant_reasoning *reasoning)
{
	/* check for duplicates */
	ptree_and_node *and = reasoning->start_node;
	while (and) {
		/*
		 * avoid duplicates
		 */
		if (tagsistant_and_node_match(and, T)) return (0);

		ptree_and_node *related = and->related;
		while (related && related->tag) {
			if (tagsistant_and_node_match(related, T)) return (0);
			related = related->related;
		}

		and = and->next;
	}

	/* adding tag */
	ptree_and_node *reasoned = g_new0(ptree_and_node, 1);

	if (!reasoned) {
		dbg('r', LOG_ERR, "Error allocating memory");
		return (-1);
	}

	reasoned->next = NULL;
	reasoned->related = NULL;
	reasoned->tag = g_strdup(T->tag);
	reasoned->namespace = g_strdup(T->namespace);
	reasoned->key = g_strdup(T->key);
	reasoned->value = g_strdup(T->value);

	/* prepend the reasoned tag */
	reasoned->related = reasoning->current_node->related;
	reasoning->current_node->related = reasoned;

	reasoning->added_tags += 1;
	return (reasoning->added_tags);
}

/**
 * SQL callback. Add new tag derived from reasoning to a ptree_and_node_t structure.
 *
 * @param _reasoning pointer to be casted to reasoning_t* structure
 * @param result dbi_result pointer
 * @return 0 always, due to SQLite policy, may change in the future
 */
static int tagsistant_add_reasoned_tag_callback(void *_reasoning, dbi_result result)
{
	/* point to a reasoning_t structure */
	tagsistant_reasoning *reasoning = (tagsistant_reasoning *) _reasoning;


	tagsistant_tag *T = g_new0(tagsistant_tag, 1);

	/* get tag components */
	gchar *tag_or_namespace = NULL, *key = NULL, *value = NULL;

	tag_or_namespace = dbi_result_get_string_copy_idx(result, 1);
	key = dbi_result_get_string_copy_idx(result, 2);
	value = dbi_result_get_string_copy_idx(result, 3);

	if (g_regex_match_simple(":$", tag_or_namespace, 0, 0)) {
		strcpy(T->namespace, tag_or_namespace);
		strcpy(T->key, key);
		strcpy(T->value, value);
	} else {
		strcpy(T->tag, tag_or_namespace);
	}

	/* add the tag */
	if (-1 == tagsistant_add_reasoned_tag(T, reasoning)) {
		g_free(T);
		dbg('r', LOG_ERR, "Error adding reasoned tag (%s, %s, %s)", tag_or_namespace, key, value);
		return (1);
	}

	g_free(T);
	dbg('r', LOG_INFO, "Adding related tag (%s, %s, %s)", tag_or_namespace, key, value);
	return (0);
}

/**
 * Search and add related tags to a ptree_and_node_t,
 * enabling tagsistant_build_filetree to later add more criteria to SQL
 * statements to retrieve files
 *
 * @param reasoning the reasoning structure the tagsistant_reasoner should work on
 */
int tagsistant_reasoner_inner(tagsistant_reasoning *reasoning, int do_caching)
{
	(void) do_caching;

#if TAGSISTANT_ENABLE_REASONER_CACHE
	GList *cached = NULL;
	gchar *key = NULL;
	int found = 0;
	gchar *reference_key = NULL;

	/*
	 * compute the key used to store the result in the reasoner cache
	 */
	if (reasoning->current_node->tag) {
		reference_key = g_strdup(reasoning->current_node->tag);
	} else if (reasoning->current_node->namespace && reasoning->current_node->key && reasoning->current_node->value) {
		reference_key = g_strdup_printf("%s<>%s<>%s",
			reasoning->current_node->namespace,
			reasoning->current_node->key,
			reasoning->current_node->value);
	}

	if (reference_key) {
		found = g_hash_table_lookup_extended(
			tagsistant_reasoner_cache,
			reference_key,
			(gpointer *) &key,
			(gpointer *) &cached);
	}

	if (found)
		// the result was cached, just add it
		g_list_foreach(cached, (GFunc) tagsistant_add_reasoned_tag, reasoning);
	else {

#endif /* TAGSISTANT_ENABLE_REASONER_CACHE*/

		tagsistant_inode other_tag_id = 0;

		if (reasoning->current_node->tag) {
			other_tag_id = tagsistant_sql_get_tag_id(reasoning->conn, reasoning->current_node->tag, NULL, NULL);
		} else if (reasoning->current_node->namespace && reasoning->current_node->key && reasoning->current_node->value) {
			other_tag_id = tagsistant_sql_get_tag_id(reasoning->conn, reasoning->current_node->namespace,
				reasoning->current_node->key, reasoning->current_node->value);
		}

		// the result wasn't cached, so we lookup it in the DB
		tagsistant_query(
			"select tagname, key, value from tags "
				"join relations on tags.tag_id = relations.tag2_id "
				"where tag1_id = %d "
			"union "
			"select tagname, key, value from tags "
				"join relations on tags.tag_id = relations.tag1_id "
				"where tag2_id = %d and relation = 'is_equivalent' ",
			reasoning->conn,
			tagsistant_add_reasoned_tag_callback,
			reasoning,
			other_tag_id,
			other_tag_id);

#if TAGSISTANT_ENABLE_REASONER_CACHE
	}

	/*
	 * Cache the result only if the cache doesn't contain it.
	 */
	if (do_caching && !found && reference_key) {
		// first we must build a GList holding all the reasoned tags...
		GList *reasoned_list = NULL;
		ptree_and_node *reasoned = reasoning->start_node->related;
		while (reasoned) {
			tagsistant_tag *T = g_new0(tagsistant_tag, 1);

			g_strlcpy(T->tag, reasoned->tag, 1024);
			g_strlcpy(T->namespace, reasoned->namespace, 1024);
			g_strlcpy(T->key, reasoned->key, 1024);
			g_strlcpy(T->value, reasoned->value, 1024);

			reasoned_list = g_list_append(reasoned_list, T);

			reasoned = reasoned->related;
		}

		// ...and then we add the tag list to the cache (do not free() the reference_key)
		g_hash_table_insert(tagsistant_reasoner_cache, reference_key, reasoned_list);
	} else {
		g_free_null(reference_key);
	}
#endif /* TAGSISTANT_ENABLE_REASONER_CACHE */

	return (reasoning->added_tags);
}

void tagsistant_invalidate_reasoning_cache(gchar *tag)
{
#if TAGSISTANT_ENABLE_REASONER_CACHE
	g_hash_table_remove(tagsistant_reasoner_cache, tag);
#else
	(void) tag;
#endif
}
