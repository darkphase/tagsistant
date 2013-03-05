/*
   Tagsistant (tagfs) -- fuse_operations/rename.c
   Copyright (C) 2006-2009 Tx0 <tx0@strumentiresistenti.org>

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
 * rename equivalent
 *
 * @param from old file name
 * @param to new file name
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_rename(const char *from, const char *to)
{
    int res = 0, tagsistant_errno = 0;

	TAGSISTANT_START("RENAME %s as %s", from, to);

	tagsistant_querytree *from_qtree = tagsistant_querytree_new(from, 1, 0);
	if (!from_qtree) TAGSISTANT_ABORT_OPERATION(ENOMEM);

	tagsistant_querytree *to_qtree = tagsistant_querytree_new(to, 1, 0);
	if (!to_qtree) TAGSISTANT_ABORT_OPERATION(ENOMEM);

	// -- malformed --
	if (QTREE_IS_MALFORMED(from_qtree)) TAGSISTANT_ABORT_OPERATION(ENOENT);

	// -- can't rename objects of different type or not both complete
	if (!QTREES_ARE_SIMILAR(from_qtree, to_qtree)) TAGSISTANT_ABORT_OPERATION(EINVAL);

	// -- can't rename anything from or into /stats or /relations
	if (QTREE_IS_STATS(to_qtree) || QTREE_IS_STATS(from_qtree) || QTREE_IS_RELATIONS(to_qtree) || QTREE_IS_RELATIONS(from_qtree))
		TAGSISTANT_ABORT_OPERATION(EINVAL);


	// -- object on disk (/archive and complete /tags) --
	if (QTREE_POINTS_TO_OBJECT(from_qtree)) {
		if (QTREE_IS_TAGGABLE(from_qtree)) {
			// 0. preserve original inode
			tagsistant_querytree_set_inode(to_qtree, from_qtree->inode);

			// 1. rename the object
			tagsistant_query(
				"update objects set objectname = \"%s\" "
					"where inode = %d",
				from_qtree->conn,
				NULL, NULL,
				to_qtree->object_path,
				from_qtree->inode);

			// 2. deletes all the tagging between "from" file and all AND nodes in "from" path
			tagsistant_querytree_traverse(from_qtree, tagsistant_sql_untag_object, from_qtree->inode);

			// 3. adds all the tags from "to" path
			tagsistant_querytree_traverse(to_qtree, tagsistant_sql_tag_object, from_qtree->inode);
		}

		// do the real rename
		res = rename(from_qtree->full_archive_path, to_qtree->full_archive_path);
		tagsistant_errno = errno;

	} else if (QTREE_IS_ROOT(from_qtree)) {
		TAGSISTANT_ABORT_OPERATION(EPERM);

	} else if (QTREE_IS_TAGS(from_qtree)) {
		if (QTREE_IS_COMPLETE(from_qtree)) {
			TAGSISTANT_ABORT_OPERATION(EPERM);
		}

		tagsistant_query(
			"update tags set tagname = \"%s\" "
				"where tagname = \"%s\"",
			from_qtree->conn,
			NULL, NULL,
			to_qtree->last_tag,
			from_qtree->last_tag);
	}

TAGSISTANT_EXIT_OPERATION:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("RENAME %s (%s) to %s (%s): %d %d: %s", from, tagsistant_querytree_type(from_qtree), to, tagsistant_querytree_type(to_qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(from_qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		tagsistant_querytree_destroy(to_qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
	} else {
		TAGSISTANT_STOP_OK("RENAME %s (%s) to %s (%s): OK", from, tagsistant_querytree_type(from_qtree), to, tagsistant_querytree_type(to_qtree));
		tagsistant_querytree_destroy(from_qtree, TAGSISTANT_COMMIT_TRANSACTION);
		tagsistant_querytree_destroy(to_qtree, TAGSISTANT_COMMIT_TRANSACTION);
	}

	return((res == -1) ? -tagsistant_errno : 0);
}
