/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

/*****************\
 * SQL FUNCTIONS *
\*****************/

#include <dbi/dbi.h>

//
// Tagsistant offers an SQLite backend but plans to implement
// a MySQL backend too. If you want to switch to experimental
// MySQL backend, use:
//
// CFLAGS="-D TAGSISTANT_SQL_BACKEND=TAGSISTANT_DBI_MYSQL_BACKEND"
//
#define TAGSISTANT_NULL_BACKEND			0
#define TAGSISTANT_DBI_MYSQL_BACKEND	1
#define TAGSISTANT_DBI_SQLITE_BACKEND	2

#ifndef TAGSISTANT_SQL_BACKEND
#	define TAGSISTANT_SQL_BACKEND TAGSISTANT_DBI_SQLITE_BACKEND
#endif

extern int tagsistant_db_connection();

/* execute SQL query adding file:line coordinates */
#define tagsistant_do_sql(statement, callback, firstarg)\
	tagsistant_real_do_sql(statement, callback, firstarg, __FILE__, (unsigned int) __LINE__)

/* real function to execute SQL statements */
extern int tagsistant_real_do_sql(char *statement, int (*callback)(void *, dbi_result), void *firstarg, char *file, unsigned int line);

/* execute SQL statements autoformatting the SQL string and adding file:line coords */
#define tagsistant_query(format, callback, firstarg, ...) \
	tagsistant_real_query(format, __FILE__, __LINE__, callback, firstarg, ## __VA_ARGS__)

/* the real code behind the previous macro */
extern int tagsistant_real_query(const char *format, gchar *file, int line, int (*callback)(void *, dbi_result), void *firstarg, ...);

/** callback to return a string */
extern int tagsistant_return_string(void *return_string, dbi_result result);

/** callback to return an integer */
extern int tagsistant_return_integer(void *return_integer, dbi_result result);

/* transactions */
extern void tagsistant_start_transaction();
extern void tagsistant_commit_transaction();
extern void tagsistant_rollback_transaction();

/***************\
 * SQL QUERIES *
\***************/

extern void				tagsistant_sql_create_tag(const gchar *tagname);
extern tagsistant_inode	tagsistant_sql_get_tag_id(const gchar *tagname);
extern void				tagsistant_sql_delete_tag(const gchar *tagname);
extern void				tagsistant_sql_tag_object(const gchar *tagname, tagsistant_inode inode);
extern void				tagsistant_sql_untag_object(const gchar *tagname, tagsistant_inode inode);
extern void				tagsistant_sql_rename_tag(const gchar *tagname, const gchar *oldtagname);
extern tagsistant_inode	tagsistant_last_insert_id();
extern int				tagsistant_object_is_tagged(tagsistant_inode inode);
extern int				tagsistant_object_is_tagged_as(tagsistant_inode inode, tagsistant_inode tag_id);
extern void				tagsistant_full_untag_object(tagsistant_inode inode);
