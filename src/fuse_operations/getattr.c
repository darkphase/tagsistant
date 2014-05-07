/*
   Tagsistant (tagfs) -- fuse_operations/getattr.c
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

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

#include "../tagsistant.h"

/**
 * Checks if a relation is valid
 *
 * @param qtree the querytree that expressed the relation in the relations/ directory
 * @param tag_id the main tag id
 * @param related_tag_id the related tag id
 * @return 1 if valid, 0 otherwise
 */
int tagsistant_valid_relation(tagsistant_querytree *qtree, tagsistant_inode tag_id, tagsistant_inode related_tag_id)
{
	int relation_is_valid = 0;

	if (g_strcmp0(qtree->relation, "is_equivalent") == 0) {
		tagsistant_query(
			"select 1 from relations "
				"where relation = 'is_equivalent' and"
					"((tag1_id = %d and tag2_id = %d) or "
					" (tag2_id = %d and tag1_id = %d))",
			qtree->dbi,
			tagsistant_return_integer,
			&relation_is_valid,
			tag_id,
			related_tag_id,
			related_tag_id,
			tag_id);
	} else if ((g_strcmp0(qtree->relation, "includes") == 0) || (g_strcmp0(qtree->relation, "excludes") == 0)) {
		tagsistant_query(
			"select 1 from relations "
				"where relation = '%s' and "
					"(tag1_id = %d and tag2_id = %d)",
			qtree->dbi,
			tagsistant_return_integer,
			&relation_is_valid,
			qtree->relation,
			tag_id,
			related_tag_id);
	}

	return (relation_is_valid);
}

/**
 * lstat equivalent
 *
 * @param path the path to be lstat()ed
 * @param stbuf pointer to struct stat buffer holding data about file
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_getattr(const char *path, struct stat *stbuf)
{
    int res = 0, tagsistant_errno = 0;
	gchar *lstat_path = NULL;

	TAGSISTANT_START("GETATTR on %s", path);

	// build querytree
	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 0, 1);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree))
		TAGSISTANT_ABORT_OPERATION(ENOENT);
	
	if (QTREE_IS_ARCHIVE(qtree)) {
		if (!g_regex_match_simple(TAGSISTANT_INODE_DELIMITER, qtree->object_path, 0, 0)) {
			lstat_path = tagsistant.archive;
		} else if (qtree->full_archive_path) {
			lstat_path = qtree->full_archive_path;
		} else {
			TAGSISTANT_ABORT_OPERATION(ENOENT);
		}
	}

	// -- object on disk --
	else if (QTREE_POINTS_TO_OBJECT(qtree)) {
		if (is_all_path(qtree->full_path)) {
			lstat_path = qtree->full_archive_path;
		} else if (tagsistant_is_tags_list_file(qtree)) {
			lstat_path = tagsistant.tags;
		} else {
			tagsistant_querytree_check_tagging_consistency(qtree);

			if (qtree->full_archive_path && qtree->exists) {
				lstat_path = qtree->full_archive_path;
			} else {
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		}
	}

	// -- alias --
	else if (QTREE_IS_ALIAS(qtree)) {
		if (qtree->alias) {
			int exists = tagsistant_sql_alias_exists(qtree->dbi, qtree->alias);
			if (!exists) {
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
			lstat_path = tagsistant.tags;
		} else {
			lstat_path = tagsistant.archive;
		}
	}

	// -- relations --
	else if (QTREE_IS_RELATIONS(qtree)) {
		tagsistant_inode tag_id = 0, related_tag_id = 0;
		lstat_path = tagsistant.archive;

		/* if ->namespace has a value, this is a triple tag */
		if (qtree->namespace) {
			tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->namespace, qtree->key, qtree->value);
			if (!tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);

			if (qtree->related_namespace) {
				related_tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->related_namespace, qtree->related_key, qtree->related_value);
				if (!related_tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);
			} else if (qtree->second_tag) {
				related_tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->second_tag, NULL, NULL);
				if (!related_tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);
			}

			// check the relation
			if (related_tag_id && (qtree->second_tag || qtree->related_value)) {
				int relation_is_valid = tagsistant_valid_relation(qtree, tag_id, related_tag_id);
				if (!relation_is_valid) TAGSISTANT_ABORT_OPERATION(ENOENT);
			}

		} else if (qtree->first_tag) {

			tagsistant_inode tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->first_tag, NULL, NULL);
			if (!tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);

			tagsistant_inode related_tag_id = 0;

			if (qtree->second_tag) {
				related_tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->second_tag, NULL, NULL);
				if (!related_tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);
			} else if (qtree->related_namespace) {
				related_tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->related_namespace, qtree->related_key, qtree->related_value);
				if (!related_tag_id) TAGSISTANT_ABORT_OPERATION(ENOENT);
			}

			// check the relation
			if (related_tag_id && (qtree->second_tag || qtree->related_value)) {
				int relation_is_valid = tagsistant_valid_relation(qtree, tag_id, related_tag_id);
				if (!relation_is_valid) TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		}
	}

	// -- stats --
	else if (QTREE_IS_STATS(qtree)) {
		if (g_regex_match_simple("^/stats/(connections|cached_queries|configuration|objects|relations|tags)$", path, 0, 0))
			lstat_path = tagsistant.tags;
		else if (g_regex_match_simple("^/stats$", path, 0, 0))
			lstat_path = tagsistant.archive;
		else
			TAGSISTANT_ABORT_OPERATION(ENOENT);
	}

	// -- store (incomplete) --
	// -- tags --
	// -- archive (the directory itself) --
	// -- root --
	else lstat_path = tagsistant.archive;

	// do the real lstat()
	res = lstat(lstat_path, stbuf);
	tagsistant_errno = errno;

	// post-processing output
	if (QTREE_IS_STORE(qtree)) {
		// dbg(LOG_INFO, "getattr: last tag is %s", qtree->last_tag);
		if (qtree->points_to_object) {
			if (tagsistant_is_tags_list_file(qtree)) {
				stbuf->st_size = TAGSISTANT_STATS_BUFFER;
			}
		} else if (NULL == qtree->last_tag) {
			// OK
		} else if (g_strcmp0(qtree->last_tag, "ALL") == 0) {
			// OK
		} else if ((g_strcmp0(qtree->last_tag, TAGSISTANT_ANDSET_DELIMITER) == 0) ||
			(g_strcmp0(qtree->last_tag, TAGSISTANT_NEGATE_NEXT_TAG) == 0)) {

			// path ends by '+' or by '!'
			stbuf->st_ino += 1;
			stbuf->st_mode = S_IFDIR|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
			stbuf->st_nlink = 1;

		} else if ((g_strcmp0(qtree->last_tag, TAGSISTANT_QUERY_DELIMITER) == 0) ||
			(g_strcmp0(qtree->last_tag, TAGSISTANT_QUERY_DELIMITER_NO_REASONING) == 0)) {

			// path ends by TAGSISTANT_QUERY_DELIMITER (with or without reasoning)
			stbuf->st_ino += 2;
			stbuf->st_mode = S_IFDIR|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
			stbuf->st_nlink = 1;

		} else if ((g_strcmp0(qtree->last_tag, TAGSISTANT_TAG_GROUP_BEGIN) == 0) ||
			(g_strcmp0(qtree->last_tag, TAGSISTANT_TAG_GROUP_END) == 0)) {

			// path ends by TAGSISTANT_QUERY_DELIMITER (with or without reasoning)
			stbuf->st_ino += 3;
			stbuf->st_mode = S_IFDIR|S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
			stbuf->st_nlink = 3;

		} else if (g_regex_match_simple("^" TAGSISTANT_ALIAS_IDENTIFIER, qtree->last_tag, 0, 0)) {
			gchar *alias_name = qtree->last_tag + 1;
			int exists = tagsistant_sql_alias_exists(qtree->dbi, alias_name);
			if (!exists) {
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}

		} else {

			tagsistant_inode tag_id;
			if (qtree->namespace) {
				tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->namespace, qtree->key, qtree->value);
			} else {
				tag_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->last_tag, NULL, NULL);
			}
			if (tag_id) {
				// each directory holds 3 inodes: itself/, itself/+, itself/@
				stbuf->st_ino = tag_id * 3;
			} else {
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}

		}
	} else if (QTREE_IS_ALIAS(qtree) && qtree->alias) {

		stbuf->st_size = tagsistant_sql_alias_get_length(qtree->dbi, qtree->alias);

	} else if (QTREE_IS_STATS(qtree)) {

		stbuf->st_size = TAGSISTANT_STATS_BUFFER;

	} else if (QTREE_IS_TAGS(qtree)) {
		gchar *tagname = qtree->first_tag ? qtree->first_tag : qtree->namespace;
		if (tagname) {
			if (qtree->second_tag) TAGSISTANT_ABORT_OPERATION(ENOENT);

			tagsistant_inode tag_id = tagsistant_sql_get_tag_id(qtree->dbi, tagname, qtree->key, qtree->value);
			if (tag_id) {
				stbuf->st_ino = tag_id * 3;
			} else {
				TAGSISTANT_ABORT_OPERATION(ENOENT);
			}
		}

	} else if (QTREE_IS_RELATIONS(qtree)) {

		// mangle inode for relations and stats

	}

TAGSISTANT_EXIT_OPERATION:

	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("GETATTR on %s (%s) {%s}: %d %d: %s", path, lstat_path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		return (-tagsistant_errno);
	} else {
		TAGSISTANT_STOP_OK("GETATTR on %s (%s): OK", path, tagsistant_querytree_type(qtree));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);
		return (0);
	}
}
