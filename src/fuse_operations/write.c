/*
   Tagsistant (tagfs) -- fuse_operations/write.c
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
    int res = 0, tagsistant_errno = 0;

	init_time_profile();
	start_time_profile();

	TAGSISTANT_START("/ WRITE on %s [size: %d offset: %lu]", path, size, (long unsigned int) offset);

	tagsistant_querytree_t *qtree = tagsistant_build_querytree(path, 0);

	// -- malformed --
	if (QTREE_IS_MALFORMED(qtree)) {
		res = -1;
		tagsistant_errno = ENOENT;
	} else

	// -- object on disk --
	if (QTREE_POINTS_TO_OBJECT(qtree)) {
		int fd = tagsistant_internal_open(qtree, fi->flags|O_WRONLY, &tagsistant_errno);
		if (fd != -1) {
			res = pwrite(fd, buf, size, offset);
			tagsistant_errno = errno;
			close(fd);
		} else {
			res = -1;
			tagsistant_errno = errno;
		}
	} else

	// -- tags --
	// -- stats --
	// -- relations --
	{
		res = -1;
		tagsistant_errno = EROFS;
	}

	stop_labeled_time_profile("write");

	if ( res == -1 ) {
		TAGSISTANT_STOP_ERROR("\\ WRITE %s (%s) (%s): %d %d: %s", path, qtree->full_archive_path, tagsistant_query_type(qtree), res, tagsistant_errno, strerror(tagsistant_errno));
	} else {
		TAGSISTANT_STOP_OK("\\ WRITE %s (%s): OK", path, tagsistant_query_type(qtree));
	}

	tagsistant_destroy_querytree(qtree);
	return((res == -1) ? -tagsistant_errno : res);
}