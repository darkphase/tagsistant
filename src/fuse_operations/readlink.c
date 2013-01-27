/*
   Tagsistant (tagfs) -- fuse_operations/readlink.c
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
 * readlink equivalent
 *
 * @param path the path of the symlink to be read
 * @param buf the path the symlink is pointing to
 * @param size length of pointed path
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_readlink(const char *path, char *buf, size_t size)
{
    int res = 0, tagsistant_errno = 0;
	gchar *readlink_path = NULL;

	TAGSISTANT_START("/ READLINK on %s", path);

	// build querytree
	tagsistant_querytree_t *qtree = tagsistant_querytree_new(path, 0);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree)) {
		res = -1;
		tagsistant_errno = ENOENT;
		goto READLINK_EXIT;
	}

	if ((QTREE_IS_TAGS(qtree) && QTREE_IS_COMPLETE(qtree)) || QTREE_IS_ARCHIVE(qtree)) {
		readlink_path = qtree->object_path;
		readlink_path = qtree->full_archive_path;
	} else if (QTREE_IS_STATS(qtree) || QTREE_IS_RELATIONS(qtree)) {
		res = -1;
		tagsistant_errno = EINVAL; /* symlinks exist in archive/ and tags/ only */
		goto READLINK_EXIT;
	}

	// do real readlink()
	res = readlink(readlink_path, buf, size);
	tagsistant_errno = errno;

	// fix bug #12475
	if (res > 0) buf[res] = '\0';

READLINK_EXIT:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("\\ READLINK on %s (%s) (%s): %d %d: %s", path, readlink_path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
	} else {
		TAGSISTANT_STOP_OK("\\ REALINK on %s (%s): OK", path, tagsistant_querytree_type(qtree));
	}

	tagsistant_querytree_destroy(qtree);
	return((res == -1) ? -tagsistant_errno : 0);
}
