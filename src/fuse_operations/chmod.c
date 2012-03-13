/*
   Tagsistant (tagfs) -- fuse_operations/chmod.c
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
 * chmod equivalent
 *
 * @param path the path to be chmod()ed
 * @param mode new mode for path
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_chmod(const char *path, mode_t mode)
{
    int res = 0, tagsistant_errno = 0;

	TAGSISTANT_START("/ CHMOD on %s [mode: %d]", path, mode);

	tagsistant_querytree_t *qtree = tagsistant_build_querytree(path, 0);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree)) {
		res = -1;
		tagsistant_errno = ENOENT;
	} else

	// -- object on disk --
	if (QTREE_POINTS_TO_OBJECT(qtree)) {
		res = chmod(qtree->full_archive_path, mode);
		tagsistant_errno = errno;
	} else

	// -- tags --
	// -- stats --
	// -- relations --
	{
		res = -1;
		tagsistant_errno = EROFS;
	}

	stop_labeled_time_profile("chmod");

	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("\\ CHMOD %s (%s) as %d: %d %d: %s", qtree->full_archive_path, tagsistant_query_type(qtree), mode, res, tagsistant_errno, strerror(tagsistant_errno));
	} else {
		TAGSISTANT_STOP_OK("\\ CHMOD %s (%s), %d: OK", path, tagsistant_query_type(qtree), mode);
	}

	tagsistant_destroy_querytree(qtree);
	return((res == -1) ? -tagsistant_errno : 0);
}