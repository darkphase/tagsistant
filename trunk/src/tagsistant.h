/*
   Tagsistant (tagfs) -- tagsistant.h
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

   Tagsistant (tagfs) mount binary written using FUSE userspace library.
   Header file

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

/** Tagsistant plugin prefix */
#define TAGSISTANT_PLUGIN_PREFIX "libtagsistant_"

/** Query delimiter as a string */
#define TAGSISTANT_QUERY_DELIMITER "@"

/** Query delimiter as a single char */
#define TAGSISTANT_QUERY_DELIMITER_CHAR '@'

/** Query delimiter without reasoning as a string */
#define TAGSISTANT_QUERY_DELIMITER_NO_REASONING "@@"

/** Query delimiter without reasoning as a single char */
#define TAGSISTANT_QUERY_DELIMITER_NO_REASONING_CHAR '@'

/** And-set delimiter as a string */
#define TAGSISTANT_ANDSET_DELIMITER "+"

/** And-set delimiter as a single char */
#define TAGSISTANT_ANDSET_DELIMITER_CHAR '+'

/** the string used to separate inodes from filenames inside archive/ directory */
#define TAGSISTANT_INODE_DELIMITER "___"

/** use an hash table to save previously processed querytrees */
#define TAGSISTANT_ENABLE_QUERYTREE_CACHE 0

/** cache tag IDs? */
#define TAGSISTANT_ENABLE_TAG_ID_CACHE 1

/** cache inode resolution queries? */
#define TAGSISTANT_ENABLE_AND_SET_CACHE 1

/** cache reasoner queries? */
#define TAGSISTANT_ENABLE_REASONER_CACHE 1

/** enable deduplication? */
#define TAGSISTANT_ENABLE_DEDUPLICATION 1

/** enable the autotagging plugin stack? */
#define TAGSISTANT_ENABLE_AUTOTAGGING 0

/** enable filehandle caching between open(), read(), write() and release() calls */
#define TAGSISTANT_ENABLE_FILE_HANDLE_CACHING 1

/** enable verbose logging, useful during debugging only */
#define TAGSISTANT_VERBOSE_LOGGING 0

/** the maximum length of the buffer used to store dynamic /stats files */
#define TAGSISTANT_STATS_BUFFER 2048

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef VERSION
#define VERSION 0
#endif

#ifndef PLUGINS_DIR
#define PLUGINS_DIR "/usr/local/lib/tagsistant/"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef linux
/* For pread()/pwrite() */
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#define _XOPEN_SOURCE 500
#endif

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif

#define _REENTRANT
#define _POSIX_PTHREAD_SEMANTICS

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#if FUSE_USE_VERSION == 25
#	if HAVE_SYS_STATVFS_H
#		include <sys/statvfs.h>
# endif
#else
#	if HAVE_SYS_STATFS_H
#		include <sys/statfs.h>
#	endif
#endif
#include <stddef.h>
#include <netinet/in.h>
#include <utime.h>
#include <signal.h>
#include <dlfcn.h> /* for dlopen() and friends */
#include <glib.h>
#include <inttypes.h>
#include <stdint.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#include "compat/fuse_opt.h"
#include <extractor.h>

/** define libextractor support */
#if (EXTRACTOR_VERSION & 0x00050000) == 0x00050000
#	define TAGSISTANT_EXTRACTOR 5          // libextractor 0.5.x
#elif (EXTRACTOR_VERSION & 0x00060000) == 0x00060000
#	define TAGSISTANT_EXTRACTOR 6          // libextractor 0.6.x
#else
#	define TAGSISTANT_EXTRACTOR 0          // no support for libextractor
#endif

/**
 * each object is identified by a unique number of type tagsistant_id
 */
typedef uint32_t tagsistant_inode;

#define dyn_strcat(original, newstring) original = _dyn_strcat(original, newstring)
extern gchar *_dyn_strcat(gchar *original, const gchar *newstring);

#include "debug.h"
#include "sql.h"
#include "path_resolution.h"
#include "plugin.h"

/**
 * defines command line options for tagsistant mount tool
 */
struct tagsistant {
	char *debug;		/**< debug profile */
	char dbg[128];		/**< debug flags */
	int	foreground;		/**< run in foreground */
	int	singlethread;	/**< single thread? */
	int	readonly;		/**< mount filesystem readonly */
	int	verbose;		/**< do verbose logging on syslog (stderr is always verbose) */
	int	quiet;			/**< don't log anything, even errors */
	int	show_config;	/**< show whole configuration */

	char *progname;		/**< tagsistant */
	char *mountpoint;	/**< no clue? */
	char *repository;	/**< it's where files and tags are archived, no? */
	char *archive;		/**< a directory holding all the files */
	char *tags;			/**< a SQLite database on file */
	char *dboptions;	/**< database options for DBI */

	/** the list of available plugins */
	tagsistant_plugin_t *plugins;

	/** debug file descriptor */
	FILE *debugfd;

	/** SQL backend provides intersect keyword */
	int sql_backend_have_intersect;

	/** SQL database driver */
	int sql_database_driver;
};

/** where global values are stored */
extern struct tagsistant tagsistant;

/** the asynchronous queue used to pass files to the deduplication/autotagging thread */
extern GAsyncQueue *tagsistant_dedup_autotag_queue;

/**
 * g_free() a symbol only if it's not NULL
 *
 * @param symbol the symbol to free
 * @return
 */
#define g_free_null(symbol) {\
	if (symbol) {\
		g_free(symbol);\
		symbol = NULL;\
	}\
}

/**
 * Fuse operations logging macros.
 * Enabled or disabled by TAGSISTANT_VERBOSE_LOGGING.
 */
#if TAGSISTANT_VERBOSE_LOGGING

#	define TAGSISTANT_START(line, ...) dbg('f', LOG_INFO, line, ##__VA_ARGS__);
#	define TAGSISTANT_STOP_OK(line, ...) dbg('f', LOG_INFO, line, ##__VA_ARGS__);
#	define TAGSISTANT_STOP_ERROR(line,...) dbg('f', LOG_ERR, line, ##__VA_ARGS__);

#else

#	define TAGSISTANT_START(line, ...) {}
#	define TAGSISTANT_STOP_OK(line, ...) {}
#	define TAGSISTANT_STOP_ERROR(line,...) {}

#endif // TAGSISTANT_VERBOSE_LOGGING

// some init functions
extern void tagsistant_utils_init();
extern void tagsistant_init_syslog();
extern void tagsistant_plugin_loader();
extern void tagsistant_plugin_unloader();

// call the plugin stack
extern int tagsistant_process(tagsistant_querytree *qtree);

// used by plugins to apply regex to file content
extern void tagsistant_plugin_apply_regex(const tagsistant_querytree *qtree, const char *buf, GMutex *m, GRegex *rx);

// create and tag a new object in one single operation
#define tagsistant_create_and_tag_object(qtree, errno) tagsistant_inner_create_and_tag_object(qtree, errno, 0);
#define tagsistant_force_create_and_tag_object(qtree, errno) tagsistant_inner_create_and_tag_object(qtree, errno, 1);
extern int tagsistant_inner_create_and_tag_object(tagsistant_querytree *qtree, int *tagsistant_errno, int force_create);

/**
 * invalidate object checksum
 *
 * @param inode the object inode
 * @param dbi_conn a valid DBI connection
 */
#define tagsistant_invalidate_object_checksum(inode, dbi_conn)\
	tagsistant_query("update objects set checksum = \"\" where inode = %d", dbi_conn, NULL, NULL, inode)

// read and write repository.ini file
extern GKeyFile *tagsistant_ini;
extern void tagsistant_manage_repository_ini();
#define tagsistant_get_ini_entry(section, key) g_key_file_get_value(tagsistant_ini, section, key, NULL)

#include "fuse_operations/operations.h"

#ifndef O_NOATIME
#define O_NOATIME	01000000
#endif

#if TAGSISTANT_ENABLE_FILE_HANDLE_CACHING
#	define tagsistant_set_file_handle(fi, fh_value) fi->fh = (unsigned long) fh_value
#	define tagsistant_get_file_handle(fi, fh_variable) fh_variable = (long) fi->fh
#else
#	define tagsistant_set_file_handle(fi, fh_value) ()
#	define tagsistant_get_file_handle(fi, fh_variable) ()
#endif
