/*
   Tagsistant (tagfs) -- deduplication.c
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

   Tagsistant (tagfs) mount binary written using FUSE userspace library.

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
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/****************************************************************************/
/***                                                                      ***/
/***   Checksumming and deduplication support                             ***/
/***                                                                      ***/
/****************************************************************************/

#if TAGSISTANT_ENABLE_AUTOTAGGING && TAGSISTANT_ENABLE_AUTOTAGGING_THREAD
GThread *tagsistant_autotag_thread = NULL;
GAsyncQueue *tagsistant_autotag_queue = NULL;
#endif

GMutex tagsistant_deduplication_mutex;
GHashTable *tagsistant_deduplication_table;

#define TAGSISTANT_DO_AUTOTAGGING 1
#define TAGSISTANT_DONT_DO_AUTOTAGGING 0

/**
 * deduplication function called by tagsistant_calculate_object_checksum
 *
 * @param inode the object inode
 * @param hex the checksum string
 * @param dbi DBI connection handle
 * @return true if autotagging is requested, false otherwise
 */
int tagsistant_querytree_find_duplicates(tagsistant_querytree *qtree, gchar *hex)
{
	tagsistant_inode main_inode = 0;

	/*
	 * get the first inode matching the checksum
	 */
	tagsistant_query(
		"select inode from objects where checksum = '%s' order by inode limit 1",
		qtree->dbi,	tagsistant_return_integer, &main_inode,	hex);

	/*
	 * if main_inode is zero, something gone wrong, we must
	 * return here, but auto-tagging can be performed
	 */
	if (!main_inode) {
		dbg('2', LOG_ERR, "Inode 0 returned for checksum %s", hex);
		return (TAGSISTANT_DO_AUTOTAGGING);
	}

	/*
	 * if this is the only copy of the file, we can
	 * return and auto-tagging can be performed
	 */
	if (qtree->inode == main_inode) return (TAGSISTANT_DO_AUTOTAGGING);

	dbg('2', LOG_INFO, "Deduplicating %s: %d -> %d", qtree->full_archive_path, qtree->inode, main_inode);

	/* first move all the tags of qtree->inode to main_inode */
	tagsistant_query(
		"update tagging set inode = %d where inode = %d",
		qtree->dbi,	NULL, NULL,	main_inode,	qtree->inode);

	/* then delete records left because of duplicates in key(inode, tag_id) in the tagging table */
	tagsistant_query(
		"delete from tagging where inode = %d",
		qtree->dbi,	NULL, NULL,	qtree->inode);

	/* unlink the removable inode */
	tagsistant_query(
		"delete from objects where inode = %d",
		qtree->dbi, NULL, NULL,	qtree->inode);

	/* and finally delete it from the archive directory */
	qtree->schedule_for_unlink = 1;

#if TAGSISTANT_ENABLE_AND_SET_CACHE
	/*
	 * invalidate the and_set cache
	 */
	tagsistant_invalidate_and_set_cache_entries(qtree);
#endif

	// don't do autotagging, the file has gone
	return (TAGSISTANT_DONT_DO_AUTOTAGGING);
}

/**
 * kernel of the deduplication thread
 *
 * @param data the path to be deduplicated (must be casted back to gchar*)
 */
gpointer tagsistant_deduplication_kernel(gpointer data)
{
	gchar *path = (gchar *) data;
	tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 1, 1);

	if (qtree) {
		int fd = open(qtree->full_archive_path, O_RDONLY|O_NOATIME);

		if (-1 != fd) {
			GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA1);
			guchar *buffer = g_new0(guchar, 65535);

			if (checksum && buffer) {
				/* feed the checksum object */
				int length = 0;
				do {
					length = read(fd, buffer, 65535);
					g_checksum_update(checksum, buffer, length);
				} while (length);

				/* get the hexadecimal checksum string */
				gchar *hex = g_strdup(g_checksum_get_string(checksum));

				/* destroy the checksum object */
				g_checksum_free(checksum);
				g_free_null(buffer);

				/* save the string into the objects table */
				tagsistant_query(
					"update objects set checksum = '%s' where inode = %d",
					qtree->dbi, NULL, NULL, hex, qtree->inode);

				/* look for duplicated objects */
				if (tagsistant_querytree_find_duplicates(qtree, hex)) {
#if TAGSISTANT_ENABLE_AUTOTAGGING
					/*
					 * do in-place autotagging
					 */
					dbg('p', LOG_INFO, "Doing in-place autotagging on %s", path);
					tagsistant_process(qtree);
#endif
				}

				tagsistant_querytree_destroy(qtree, TAGSISTANT_COMMIT_TRANSACTION);

				/* free the hex checksum string */
				g_free_null(hex);
			}
		}

		close(fd);
	}

	/*
	 * remove the path from the deduplication table
	 */
	g_mutex_lock(&tagsistant_deduplication_mutex);
	g_hash_table_remove(tagsistant_deduplication_table, path);
	g_mutex_unlock(&tagsistant_deduplication_mutex);

	/*
	 * exit deduplication thread
	 */
	// g_free_null(path); // not required because the path is freed by g_hash_table_remove()
	g_thread_exit(NULL);
	return (NULL);
}

void tagsistant_deduplication_init()
{
	tagsistant_deduplication_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * starts deduplication thread
 *
 * @path the path to be deduplicated
 */
void tagsistant_deduplicate(gchar *path)
{
	g_mutex_lock(&tagsistant_deduplication_mutex);
	if (!g_hash_table_contains(tagsistant_deduplication_table, path)) {
		g_thread_new(path, tagsistant_deduplication_kernel, path);
		g_hash_table_insert(tagsistant_deduplication_table, path, GINT_TO_POINTER(1));
	}
	g_mutex_unlock(&tagsistant_deduplication_mutex);
}

#if 0
/**
 * Deduplicates the object pointed by the querytree
 *
 * @param qtree the querytree object
 * @return true if autotagging is required, false otherwise
 */
int tagsistant_querytree_deduplicate(tagsistant_querytree *qtree)
{
	dbg('2', LOG_INFO, "Running deduplication on %s", qtree->object_path);

	/* guess if the object is a file or a symlink */
	struct stat buf;
	if ((-1 == lstat(qtree->full_archive_path, &buf)) || (!S_ISREG(buf.st_mode) && !S_ISLNK(buf.st_mode)))
		return (TAGSISTANT_DONT_DO_AUTOTAGGING);

	dbg('2', LOG_INFO, "Checksumming %s", qtree->full_archive_path);

	/*
	 * we'll return a 'do autotagging' condition even if
	 * a problem arises in computing file checksum
	 */
	int do_autotagging = TAGSISTANT_DO_AUTOTAGGING;

	/*
	 * open the file and read its content to compute is checksum
	 */
	int fd = open(qtree->full_archive_path, O_RDONLY|O_NOATIME);
	if (-1 != fd) {
		GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA1);
		guchar *buffer = g_new0(guchar, 65535);

		if (checksum && buffer) {
			/* feed the checksum object */
			int length = 0;
			do {
				length = read(fd, buffer, 65535);
				g_checksum_update(checksum, buffer, length);
			} while (length);

			/* get the hexadecimal checksum string */
			gchar *hex = g_strdup(g_checksum_get_string(checksum));

			/* destroy the checksum object */
			g_checksum_free(checksum);
			g_free_null(buffer);

			/* save the string into the objects table */
			tagsistant_query(
				"update objects set checksum = '%s' where inode = %d",
				qtree->dbi, NULL, NULL, hex, qtree->inode);

			/* look for duplicated objects */
			do_autotagging = tagsistant_querytree_find_duplicates(qtree, hex);

			/* free the hex checksum string */
			g_free_null(hex);
		}
		close(fd);
	}

	return (do_autotagging);
}

/**
 * This is the kernel of the autotagging thread.
 */
void tagsistant_autotag_thread_kernel(gpointer data) {
	(void) data;

#if TAGSISTANT_ENABLE_AUTOTAGGING && TAGSISTANT_ENABLE_AUTOTAGGING_THREAD
	while (1) {
		// get a path from the queue
		gchar *path = (gchar *) g_async_queue_pop(tagsistant_autotag_queue);

		// process the path only if it's not null
		if (path && strlen(path)) {

			// build the querytree from the path
			tagsistant_querytree *qtree = tagsistant_querytree_new(path, 0, 1, 1);
			if (!qtree) {
				int i;
				for (i = 5; i; i--) {
					sleep(1);
					qtree = tagsistant_querytree_new(path, 0, 1, 1);
				}
			}

			// run the autotagging plugin stack
			tagsistant_process(qtree);

			// destroy the querytree
			tagsistant_querytree_destroy(qtree, 1);
		}

		g_free_null(path);
	}
#endif
}
#endif
