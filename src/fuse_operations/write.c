/*
   Tagsistant (tagfs) -- fuse_operations/write.c
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
 * write() equivalent
 *
 * @param path the path of the file to be written
 * @param buf buffer holding write() data
 * @param size how many bytes should be written (size of *buf)
 * @param offset starting of the write
 * @param fi struct fuse_file_info used for open() flags
 * @return(0 on success, -errno otherwise)
 */
int tagsistant_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res = 0, tagsistant_errno = 0, fh = 0;

	TAGSISTANT_START("WRITE on %s [size: %lu offset: %lu]", path, (unsigned long) size, (long unsigned int) offset);

	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 0, 1);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree))
		TAGSISTANT_ABORT_OPERATION(ENOENT);

	// -- object on disk --
	if (QTREE_POINTS_TO_OBJECT(qtree)) {
		if (!qtree->full_archive_path) {
			dbg('F', LOG_ERR, "Null qtree->full_archive_path");
			TAGSISTANT_ABORT_OPERATION(EFAULT);
		}

#if TAGSISTANT_ENABLE_FILE_HANDLE_CACHING
		if (fi->fh) {
			tagsistant_get_file_handle(fi, fh);
			res = pwrite(fh, buf, size, offset);
			tagsistant_errno = errno;
		}

		if ((-1 == res) || (0 == fh)) {
			if (fh) close(fh);
			fh = open(qtree->full_archive_path, fi->flags|O_WRONLY);

			if (fh)	res = pwrite(fh, buf, size, offset);
			else res = -1;
			tagsistant_errno = errno;
		}

		tagsistant_set_file_handle(fi, fh);
#else
		fh = open(qtree->full_archive_path, fi->flags|O_WRONLY);
		if (fh) {
			res = pwrite(fh, buf, size, offset);
			tagsistant_errno = errno;
			close(fh);
		} else {
			TAGSISTANT_ABORT_OPERATION(errno);
		}
#endif
	}

	// -- tags --
	// -- stats --
	// -- relations --
	else TAGSISTANT_ABORT_OPERATION(EROFS);

TAGSISTANT_EXIT_OPERATION:
	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("WRITE %s (%s) (%s): %d %d: %s", path, qtree->full_archive_path, tagsistant_querytree_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_ROLLBACK_TRANSACTION);
		return (-tagsistant_errno);
	} else {
		TAGSISTANT_STOP_OK("WRITE %s (%s): OK", path, tagsistant_querytree_type(qtree));
		tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);
		return (res);
	}
}
