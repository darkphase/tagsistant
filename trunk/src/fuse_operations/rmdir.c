/*
   Tagsistant (tagfs) -- fuse_operations/rmdir.c
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
 * rmdir equivalent
 *
 * @param path the tag (directory) to be removed
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_rmdir(const char *path)
{
    int res = 0, tagsistant_errno = 0, do_rmdir = 1;
	gchar *rmdir_path = NULL;

	TAGSISTANT_START("RMDIR on %s", path);

	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 1);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree)) {
		TAGSISTANT_ABORT_OPERATION(ENOENT);
	}

	// -- tags --
	// tags/delete_this_tag
	// tags/delete_this_tag/@/
	// tags/tag/@/delete_this_dir
	// tags/tag/@/dir/delete_this_dir
	//
	if (QTREE_IS_TAGS(qtree)) {
		tagsistant_querytree_check_tagging_consistency(qtree);

		if (!QTREE_IS_COMPLETE(qtree)) {
			// -- tags but incomplete (means: delete a tag) --
			tagsistant_querytree_traverse(qtree, tagsistant_sql_delete_tag);
			do_rmdir = 0;
		} else if (QTREE_IS_TAGGABLE(qtree)) {
			/*
			 * if object is pointed by a tags/ query, then untag it
			 * from the tags included in the query path...
			 */
			tagsistant_querytree_traverse(qtree, tagsistant_sql_untag_object, qtree->inode);

			/*
			 * ...then check if it's tagged elsewhere...
			 * ...if still tagged, then avoid real unlink(): the object must survive!
			 * ...otherwise we can delete it from the objects table
			 */
			if (!tagsistant_object_is_tagged(qtree->dbi, qtree->inode)) {
				tagsistant_query(
					"delete from objects where inode = %d",
					qtree->dbi, NULL, NULL, qtree->inode);
			} else {
				do_rmdir = 0;
			}
		}

		// do a real mkdir
		if (do_rmdir) {
			rmdir_path = qtree->full_archive_path;
			res = rmdir(rmdir_path);
			tagsistant_errno = errno;

		}
	}

	// -- relations --
	else if (QTREE_IS_RELATIONS(qtree)) {
		// rmdir can be used only on third level
		// since first level is all available tags
		// and second level is all available relations
		if (qtree->second_tag) {
			// create a new relation between two tags
			tagsistant_sql_create_tag(qtree->dbi, qtree->second_tag);
			int tag1_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->first_tag);
			int tag2_id = tagsistant_sql_get_tag_id(qtree->dbi, qtree->second_tag);
			if (tag1_id && tag2_id && IS_VALID_RELATION(qtree->relation)) {
				tagsistant_query(
					"delete from relations where tag1_id = \"%d\" and tag2_id = \"%d\" and relation = \"%s\"",
					qtree->dbi, NULL, NULL, tag1_id, tag2_id, qtree->relation);

#if TAGSISTANT_ENABLE_QUERYTREE_CACHE
				// invalidate the cache entries which involves one of the tags related
				tagsistant_invalidate_querytree_cache(qtree);
#endif

				tagsistant_invalidate_reasoning_cache(qtree->first_tag);
				tagsistant_invalidate_reasoning_cache(qtree->second_tag);
			} else {
				res = -1;
				tagsistant_errno = EFAULT;
			}
		} else {
			res = -1;
			tagsistant_errno = EROFS;
		}
	}

	// -- archive
	// -- stats
	else if (QTREE_IS_STATS(qtree)) TAGSISTANT_ABORT_OPERATION(EROFS);


TAGSISTANT_EXIT_OPERATION:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("RMDIR on %s (%s): %d %d: %s", path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
	} else {
		TAGSISTANT_STOP_OK("RMDIR on %s (%s): OK", path, tagsistant_querytree_type(qtree));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);
	}

	return((res == -1) ? -tagsistant_errno : 0);
}
