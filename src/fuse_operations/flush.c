/*
   Tagsistant (tagfs) -- fuse_operations/open.c
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
 * close() equivalent [first part, second is tagsistant_release()]
 *
 * @param path the path to be open()ed
 * @param fi struct fuse_file_info holding open() flags
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_flush(const char *path, struct fuse_file_info *fi)
{
    int res = 0, tagsistant_errno = 0;
    (void) fi;

	TAGSISTANT_START("FLUSH on %s", path);

	// build querytree
	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 0, 1);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree))
		TAGSISTANT_ABORT_OPERATION(ENOENT);

#if TAGSISTANT_ENABLE_DEDUPLICATION || TAGSISTANT_ENABLE_AUTOTAGGING
	// check if the file has been modified
	int modified = 0;
	tagsistant_query(
		"select inode from objects where inode = %d and checksum = ''",
		qtree->dbi, tagsistant_return_integer, &modified, qtree->inode);

	if (modified) {
#	if TAGSISTANT_DEDUPLICATION_THREAD
		/*
		 * schedule deduplication and autotagging for this file
		 */
		dbg('2', LOG_INFO, "Scheduling %s for deduplication", path);
		g_async_queue_push(tagsistant_dedup_autotag_queue, g_strdup(path));
#	else
		/*
		 * do in-place deduplication and autotagging
		 */

#		if TAGSISTANT_ENABLE_DEDUPLICATION
		if (tagsistant_querytree_deduplicate(qtree)) { /* ............ deduplication */
#		endif

#		if TAGSISTANT_ENABLE_AND_SET_CACHE
			tagsistant_invalidate_and_set_cache_entries(qtree); /* ... invalidate the and_set cache */
#		endif

#		if TAGSISTANT_ENABLE_AUTOTAGGING
			tagsistant_process(qtree); /* ............................ run the autotagging plugin stack */
#		endif

#		if TAGSISTANT_ENABLE_DEDUPLICATION
		}
#		endif

#	endif
		}
#endif

	if (fi->fh) {
		dbg('F', LOG_INFO, "Uncaching %" PRIu64 " = open(%s)", fi->fh, path);
//		fprintf(stderr, "Closing FD %lu\n", fi->fh);
		close(fi->fh);
		fi->fh = 0;
	}

TAGSISTANT_EXIT_OPERATION:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("FLUSH on %s (%s) (%s): %d %d: %s", path, qtree->full_archive_path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		return (-tagsistant_errno);
	} else {
		TAGSISTANT_STOP_OK("FLUSH on %s (%s): OK", path, tagsistant_querytree_type(qtree));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);
		return (0);
	}
}
