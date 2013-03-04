/*
   Tagsistant (tagfs) -- fuse_operations/read.c
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
 * read() equivalent
 *
 * @param path the path of the file to be read
 * @param buf buffer holding read() result
 * @param size how many bytes should/can be read
 * @param offset starting of the read
 * @param fi struct fuse_file_info used for open() flags
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res = 0, tagsistant_errno = 0;

	TAGSISTANT_START("/ READ on %s [size: %lu offset: %lu]", path, (long unsigned int) size, (long unsigned int) offset);

	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 1, 0);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree)) {
		TAGSISTANT_ABORT_OPERATION(ENOENT);
	}

	// -- object on disk --
	else if (QTREE_POINTS_TO_OBJECT(qtree)) {
		if ((!qtree) || (!qtree->full_archive_path)) {
			dbg(LOG_ERR, "Null qtree or qtree->full_archive path");
			TAGSISTANT_ABORT_OPERATION(EFAULT);
		}

		int fd = open(qtree->full_archive_path, fi->flags|O_RDONLY);

		if (-1 == fd) {
			TAGSISTANT_ABORT_OPERATION(errno);
		}

		res = pread(fd, buf, size, offset);
		tagsistant_errno = errno;
		close(fd);
	}

	// -- stats --
	else if (QTREE_IS_STATS(qtree)) {
		// do what is needed
	}

	// -- tags --
	// -- relations --
	else TAGSISTANT_ABORT_OPERATION(EROFS);

TAGSISTANT_EXIT_OPERATION:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("\\ READ %s (%s) (%s): %d %d: %s", path, qtree->full_archive_path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
	} else {
		TAGSISTANT_STOP_OK("\\ READ %s (%s): OK", path, tagsistant_querytree_type(qtree));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);
	}

	return((res == -1) ? -tagsistant_errno : res);
}

