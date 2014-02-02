/*
   Tagsistant (tagfs) -- sql.c
   Copyright (C) 2006-2010 Tx0 <tx0@strumentiresistenti.org>

   Tagsistant (tagfs) DBI-based SQL backend

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

GMutex tagsistant_query_mutex;

/**
 * check if requested driver is provided by local DBI installation
 * 
 * @param driver_name string with the name of the driver
 * @return 1 if exists, 0 otherwise
 */
int tagsistant_driver_is_available(const char *driver_name)
{
	int counter = 0;
	int driver_found = 0;
	dbi_driver driver = NULL;

	dbg('b', LOG_INFO, "Available drivers:");
	while ((driver = dbi_driver_list(driver)) != NULL) {
		counter++;
		dbg('b', LOG_INFO, "  Driver #%d: %s - %s", counter, dbi_driver_get_name(driver), dbi_driver_get_filename(driver));
		if (g_strcmp0(dbi_driver_get_name(driver), driver_name) == 0) {
			driver_found = 1;
		}
	}

	if (!counter) {
		dbg('b', LOG_ERR, "No SQL driver found! Exiting now.");
		return(0);
	}

	if (!driver_found) {
		dbg('b', LOG_ERR, "No %s driver found!", driver_name);
		return(0);
	}

	return(1);
}

/**
 * Contains DBI parsed options
 */
struct {
	int backend;
	gchar *backend_name;
	gchar *host;
	gchar *db;
	gchar *username;
	gchar *password;
} dboptions;

#if TAGSISTANT_ENABLE_TAG_ID_CACHE
/** a map tag_name -> tag_id */
GHashTable *tagsistant_tag_cache = NULL;
#endif

/** regular expressions used to escape query parameters */
GRegex *RX1, *RX2, *RX3;

/**
 * Initialize libDBI structures
 */
void tagsistant_db_init()
{
	// initialize DBI library
	dbi_initialize(NULL);

	g_mutex_init(&tagsistant_query_mutex);

#if TAGSISTANT_ENABLE_TAG_ID_CACHE
	tagsistant_tag_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
#endif

	// by default, DBI backend provides intersect
	tagsistant.sql_backend_have_intersect = 1;
	tagsistant.sql_database_driver = TAGSISTANT_NULL_BACKEND;
	dboptions.backend = TAGSISTANT_NULL_BACKEND;

	// if no database option has been passed, use default SQLite3
	if (strlen(tagsistant.dboptions) == 0) {
		tagsistant.dboptions = g_strdup("sqlite3::::");
		dboptions.backend_name = g_strdup("sqlite3");
		dboptions.backend = TAGSISTANT_DBI_SQLITE_BACKEND;
		dbg('b', LOG_INFO, "Using default driver: sqlite3");
	}

	dbg('b', LOG_INFO, "Database options: %s", tagsistant.dboptions);

	// split database option value up to 5 tokens
	gchar **_dboptions = g_strsplit(tagsistant.dboptions, ":", 5);

	// set failsafe DB options
	if (_dboptions[0]) {
		if (strcmp(_dboptions[0], "sqlite3") == 0) {

			tagsistant.sql_database_driver = TAGSISTANT_DBI_SQLITE_BACKEND;
			dboptions.backend = TAGSISTANT_DBI_SQLITE_BACKEND;
			dboptions.backend_name = g_strdup("sqlite3");

		} else if (strcmp(_dboptions[0], "mysql") == 0) {

			tagsistant.sql_database_driver = TAGSISTANT_DBI_MYSQL_BACKEND;
			dboptions.backend = TAGSISTANT_DBI_MYSQL_BACKEND;
			dboptions.backend_name = g_strdup("mysql");

		}
	}

	if (TAGSISTANT_DBI_MYSQL_BACKEND == dboptions.backend) {
		if (_dboptions[1] && strlen(_dboptions[1])) {
			dboptions.host = g_strdup(_dboptions[1]);

			if (_dboptions[2] && strlen(_dboptions[2])) {
				dboptions.db = g_strdup(_dboptions[2]);

				if (_dboptions[3] && strlen(_dboptions[3])) {
					dboptions.username = g_strdup(_dboptions[3]);

					if (_dboptions[4] && strlen(_dboptions[4])) {
						dboptions.password = g_strdup(_dboptions[4]);
					} else {
						dboptions.password = g_strdup("tagsistant");
					}

				} else {
					dboptions.password = g_strdup("tagsistant");
					dboptions.username = g_strdup("tagsistant");
				}

			} else {
				dboptions.password = g_strdup("tagsistant");
				dboptions.username = g_strdup("tagsistant");
				dboptions.db = g_strdup("tagsistant");
			}

		} else {
			dboptions.password = g_strdup("tagsistant");
			dboptions.username = g_strdup("tagsistant");
			dboptions.db = g_strdup("tagsistant");
			dboptions.host = g_strdup("localhost");
		}

	}

	g_strfreev(_dboptions);

#if 0
	dbg('b', LOG_INFO, "Database driver: %s", dboptions.backend_name);

	// list configured options
	const char *option = NULL;
	int counter = 0;
	dbg('b', LOG_INFO, "Connection settings: ");
	while ((option = dbi_conn_get_option_list(tagsistant_dbi_conn, option))	!= NULL ) {
		counter++;
		dbg('b', LOG_INFO, "  Option #%d: %s = %s", counter, option, dbi_conn_get_option(tagsistant_dbi_conn, option));
	}

	// tell if backend have INTERSECT
	if (tagsistant.sql_backend_have_intersect) {
		dbg('b', LOG_INFO, "Database supports INTERSECT operator");
	} else {
		dbg('b', LOG_INFO, "Database does not support INTERSECT operator");
	}
#endif

	/* initialize the regular expressions used to escape the SQL queries */
	RX1 = g_regex_new("[\"']", 0, 0, NULL);
	RX2 = g_regex_new("\"", 0, 0, NULL);
	RX3 = g_regex_new("<><>", 0, 0, NULL);
}

/**
 * This single linked list holds the connection pool
 */
GList *tagsistant_connection_pool = NULL;
GMutex tagsistant_connection_pool_lock;
int connections = 0;

/**
 * Experimental Mutex for SQLite
 */
GMutex tagsistant_sqlite_global_lock;

/**
 * Parse command line options, create connection object,
 * start the connection and finally create database schema
 *
 * @return DBI connection handle
 */
dbi_conn *tagsistant_db_connection(int start_transaction)
{
	/* DBI connection handler used by subsequent calls to dbi_* functions */
	dbi_conn dbi = NULL;

	/* lock the pool */
	g_mutex_lock(&tagsistant_connection_pool_lock);
	GList *pool = tagsistant_connection_pool;
	while (pool) {
		dbi = (dbi_conn) pool->data;

		/* check if the connection is still alive */
		if (!dbi_conn_ping(dbi) && dbi_conn_connect(dbi) < 0) {
			dbi_conn_close(dbi);
			tagsistant_connection_pool = g_list_delete_link(tagsistant_connection_pool, pool);
			connections--;
		} else {
//			dbg('s', LOG_INFO, "Reusing DBI connection (currently %d created", connections);
			tagsistant_connection_pool = g_list_remove_link(tagsistant_connection_pool, pool);
			g_list_free_1(pool);
			break;
		}

		pool = pool->next;
	}
	g_mutex_unlock(&tagsistant_connection_pool_lock);

	if (!dbi) {
		// initialize DBI drivers
		if (TAGSISTANT_DBI_MYSQL_BACKEND == dboptions.backend) {
			if (!tagsistant_driver_is_available("mysql")) {
				fprintf(stderr, "MySQL driver not installed\n");
				dbg('s', LOG_ERR, "MySQL driver not installed");
				exit (1);
			}

			// unlucky, MySQL does not provide INTERSECT operator
			tagsistant.sql_backend_have_intersect = 0;

			// create connection
			dbi = dbi_conn_new("mysql");
			if (NULL == dbi) {
				dbg('s', LOG_ERR, "Error creating MySQL connection");
				exit (1);
			}

			// set connection options
			dbi_conn_set_option(dbi, "host",     dboptions.host);
			dbi_conn_set_option(dbi, "dbname",   dboptions.db);
			dbi_conn_set_option(dbi, "username", dboptions.username);
			dbi_conn_set_option(dbi, "password", dboptions.password);
			dbi_conn_set_option(dbi, "encoding", "UTF-8");

		} else if (TAGSISTANT_DBI_SQLITE_BACKEND == dboptions.backend) {
			if (!tagsistant_driver_is_available("sqlite3")) {
				fprintf(stderr, "SQLite3 driver not installed\n");
				dbg('s', LOG_ERR, "SQLite3 driver not installed");
				exit(1);
			}

			// create connection
			dbi = dbi_conn_new("sqlite3");
			if (NULL == dbi) {
				dbg('s', LOG_ERR, "Error connecting to SQLite3");
				exit (1);
			}

			// set connection options
			dbi_conn_set_option(dbi, "dbname", "tags.sql");
			dbi_conn_set_option(dbi, "sqlite3_dbdir", tagsistant.repository);

		} else {

			dbg('s', LOG_ERR, "No or wrong database family specified!");
			exit (1);
		}

		// try to connect
		if (dbi_conn_connect(dbi) < 0) {
			int error = dbi_conn_error(dbi, NULL);
			dbg('s', LOG_ERR, "Could not connect to DB (error %d). Please check the --db settings", error);
			exit(1);
		}

		connections++;

		dbg('s', LOG_INFO, "SQL connection established");
	}

	/* lock the global mutex for SQLite backend */
	if (tagsistant.sql_database_driver == TAGSISTANT_DBI_SQLITE_BACKEND)
		g_mutex_lock(&tagsistant_sqlite_global_lock);

	/* start a transaction */
	if (start_transaction) {

#if TAGSISTANT_USE_INTERNAL_TRANSACTIONS
		switch (tagsistant.sql_database_driver) {
			case TAGSISTANT_DBI_SQLITE_BACKEND:
				tagsistant_query("begin transaction", dbi, NULL, NULL);
				break;

			case TAGSISTANT_DBI_MYSQL_BACKEND:
				tagsistant_query("start transaction", dbi, NULL, NULL);
				break;
		}
#else
		dbi_conn_transaction_begin(dbi);
#endif
	}

	return(dbi);
}

/**
 * Release a DBI connection
 *
 * @param dbi the connection to be released
 */
void tagsistant_db_connection_release(dbi_conn dbi)
{
	// unlock the global mutex for SQLite backend
	if (tagsistant.sql_database_driver == TAGSISTANT_DBI_SQLITE_BACKEND)
		g_mutex_unlock(&tagsistant_sqlite_global_lock);

	// release the connection back to the pool
	g_mutex_lock(&tagsistant_connection_pool_lock);
	tagsistant_connection_pool = g_list_prepend(tagsistant_connection_pool, dbi);
	g_mutex_unlock(&tagsistant_connection_pool_lock);
}

/**
 * Create DB schema
 */
void tagsistant_create_schema()
{
	dbi_conn dbi = tagsistant_db_connection(TAGSISTANT_START_TRANSACTION);

	// create database schema
	switch (tagsistant.sql_database_driver) {
		case TAGSISTANT_DBI_SQLITE_BACKEND:
			tagsistant_query(
				"create table if not exists tags ("
					"tag_id integer primary key autoincrement not null, "
					"tagname varchar(65) not null, "
					"key varchar(65) not null default '', "
					"value varchar(65) not null default '', "
					"constraint Tag_key unique (tagname, key, value))",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists objects ("
					"inode integer not null primary key autoincrement, "
					"objectname text(255) not null, "
					"last_autotag timestamp not null default 0, "
					"checksum text(40) not null default '', "
					"symlink text(1024) not null default '')",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists tagging ("
					"inode integer not null, "
					"tag_id integer not null, "
					"constraint Tagging_key unique (inode, tag_id))",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists relations ("
					"relation_id integer primary key autoincrement not null, "
					"tag1_id integer not null, "
					"relation varchar not null, "
					"tag2_id integer not null)",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists aliases ("
					"alias varchar(65) primary key not null, "
					"query varchar(%d) not null)",
				dbi, NULL, NULL, TAGSISTANT_ALIAS_MAX_LENGTH);

			tagsistant_query("create index if not exists relations_index on relations (tag1_id, tag2_id)", dbi, NULL, NULL);
			tagsistant_query("create index if not exists objectname_index on objects (objectname)", dbi, NULL, NULL);
			tagsistant_query("create index if not exists symlink_index on objects (symlink, inode)", dbi, NULL, NULL);
			tagsistant_query("create index if not exists checksum_index on objects (checksum, inode)", dbi, NULL, NULL);
			tagsistant_query("create index if not exists relations_type_index on relations (relation)", dbi, NULL, NULL);
			tagsistant_query("create index if not exists aliases_index on aliases (alias)", dbi, NULL, NULL);
			break;

		case TAGSISTANT_DBI_MYSQL_BACKEND:
			tagsistant_query(
				"create table if not exists tags ("
					"tag_id integer primary key auto_increment not null, "
					"tagname varchar(65) not null, "
					"`key` varchar(65) not null, "
					"value varchar(65) not null, "
					"constraint Tag_key unique `key` (tagname, `key`, value))",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists objects ("
					"inode integer not null primary key auto_increment, "
					"objectname varchar(255) not null, "
					"last_autotag timestamp not null default 0, "
					"checksum varchar(40) not null default '', "
					"symlink varchar(1024) not null default '')",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists tagging ("
					"inode integer not null, "
					"tag_id integer not null, "
					"constraint Tagging_key unique key (inode, tag_id))",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists relations ("
					"relation_id integer primary key auto_increment not null, "
					"tag1_id integer not null, "
					"relation varchar(32) not null, "
					"tag2_id integer not null)",
				dbi, NULL, NULL);

			tagsistant_query(
				"create table if not exists aliases ("
					"alias varchar(65) primary key not null, "
					"query varchar(%d) not null)",
				dbi, NULL, NULL, TAGSISTANT_ALIAS_MAX_LENGTH);

			tagsistant_query("create index relations_index on relations (tag1_id, tag2_id)", dbi, NULL, NULL);
			tagsistant_query("create index objectname_index on objects (objectname)", dbi, NULL, NULL);
			tagsistant_query("create index symlink_index on objects (symlink, inode)", dbi, NULL, NULL);
			tagsistant_query("create index checksum_index on objects (checksum, inode)", dbi, NULL, NULL);
			tagsistant_query("create index relations_type_index on relations (relation)", dbi, NULL, NULL);
			tagsistant_query("create index aliases_index on aliases (alias)", dbi, NULL, NULL);
			break;

		default:
			break;
	}

	tagsistant_commit_transaction(dbi);
	tagsistant_db_connection_release(dbi);
}

/**
 * Prepare SQL queries and perform them.
 *
 * @param format printf-like string of SQL query
 * @param callback pointer to function to be called on results of SQL query
 * @param firstarg pointer to buffer for callback returned data
 * @return 0 (always, due to SQLite policy)
 */
int tagsistant_real_query(
	dbi_conn dbi,
	const char *format,
	int (*callback)(void *, dbi_result),
	char *file,
	int line,
	void *firstarg,
	...)
{
	va_list ap;
	va_start(ap, firstarg);

	/* check if connection has been created */
	if (NULL == dbi) {
		dbg('s', LOG_ERR, "ERROR! DBI connection was not initialized!");
		return(0);
	}

	/* lock the connection mutex */
	g_mutex_lock(&tagsistant_query_mutex);

	/* check if the connection is alive */
	if (!dbi_conn_ping(dbi)	&& dbi_conn_connect(dbi) < 0) {
		g_mutex_unlock(&tagsistant_query_mutex);
		dbg('s', LOG_ERR, "ERROR! DBI Connection has gone!");
		return(0);
	}

	/* replace all the single or double quotes with "<><>" in the format */
	gchar *escaped_format = g_regex_replace_literal(RX1, format, -1, 0, "<><>", 0, NULL);

	/* format the statement */
	gchar *statement = g_strdup_vprintf(escaped_format, ap);
	if (NULL == statement) {
		dbg('s', LOG_ERR, "Null SQL statement");
		g_free(escaped_format);
		return(0);
	}

	/* prepend a backslash to all the single quotes inside the arguments */
	gchar *escaped_statement_tmp = g_regex_replace_literal(RX2, statement, -1, 0, "\\'", 0, NULL);

	/* replace "<><>" with a single quote */
	gchar *escaped_statement = g_regex_replace_literal(RX3, escaped_statement_tmp, -1, 0, "\"", 0, NULL);

	/* log and do the query */
	dbg('s', LOG_INFO, "SQL from %s:%d: [%s]", file, line, escaped_statement);
	dbi_result result = dbi_conn_query(dbi, escaped_statement);

	tagsistant_dirty_logging(escaped_statement);

	g_free_null(escaped_format);
	g_free_null(escaped_statement_tmp);
	g_free_null(escaped_statement);

	/* call the callback function on results or report an error */
	int rows = 0;
	if (result) {

		if (callback) {
			while (dbi_result_next_row(result)) {
				callback(firstarg, result);
				rows++;
			}
		}
		dbi_result_free(result);

	} else {

		/* get the error message */
		const char *errmsg = NULL;
		dbi_conn_error(dbi, &errmsg);
		if (errmsg) dbg('s', LOG_ERR, "Error: %s.", errmsg);

	}

	g_mutex_unlock(&tagsistant_query_mutex);

	return(rows);
}

/**
 * return(last insert row inode)
 */
tagsistant_inode tagsistant_last_insert_id(dbi_conn conn)
{
	return(dbi_conn_sequence_last(conn, NULL));

#if 0
	// -------- alternative version -----------------------------------------------

	tagsistant_inode inode = 0;

	switch (tagsistant.sql_database_driver) {
		case TAGSISTANT_DBI_SQLITE_BACKEND:
			tagsistant_query("SELECT last_insert_rowid() ", conn, tagsistant_return_integer, &inode);
			break;

		case TAGSISTANT_DBI_MYSQL_BACKEND:
			tagsistant_query("SELECT last_insert_id() ", conn, tagsistant_return_integer, &inode);
			break;
	}

	return (inode);
#endif
}

/**
 * SQL callback. Return an integer from a query
 *
 * @param return_integer integer pointer cast to void* which holds the integer to be returned
 * @param result dbi_result pointer
 * @return 0 (always, due to SQLite policy, may change in the future)
 */
int tagsistant_return_integer(void *return_integer, dbi_result result)
{
	uint32_t *buffer = (uint32_t *) return_integer;
	*buffer = 0;

	unsigned int type = dbi_result_get_field_type_idx(result, 1);
	if (type == DBI_TYPE_INTEGER) {
		unsigned int size = dbi_result_get_field_attribs_idx(result, 1);
		unsigned int is_unsigned = size & DBI_INTEGER_UNSIGNED;
		size = size & DBI_INTEGER_SIZEMASK;
		switch (size) {
			case DBI_INTEGER_SIZE8:
				if (is_unsigned)
					*buffer = dbi_result_get_ulonglong_idx(result, 1);
				else
					*buffer = dbi_result_get_longlong_idx(result, 1);
				break;
			case DBI_INTEGER_SIZE4:
			case DBI_INTEGER_SIZE3:
				if (is_unsigned)
					*buffer = dbi_result_get_uint_idx(result, 1);
				else
					*buffer = dbi_result_get_int_idx(result, 1);
				break;
			case DBI_INTEGER_SIZE2:
				if (is_unsigned)
					*buffer = dbi_result_get_ushort_idx(result, 1);
				else
					*buffer = dbi_result_get_short_idx(result, 1);
				break;
			case DBI_INTEGER_SIZE1:
				if (is_unsigned)
					*buffer = dbi_result_get_uchar_idx(result, 1);
				else
					*buffer = dbi_result_get_char_idx(result, 1);
				break;
		}
	} else if (type == DBI_TYPE_DECIMAL) {
		return (0);
	} else if (type == DBI_TYPE_STRING) {
		const gchar *int_string = dbi_result_get_string_idx(result, 1);
		*buffer = atoi(int_string);
		dbg('s', LOG_INFO, "tagsistant_return_integer called on non integer field");
	}

	dbg('s', LOG_INFO, "Returning integer: %d", *buffer);

	return (0);
}

/**
 * SQL callback. Return a string from a query
 * Should be called as in:
 *
 *   gchar *string;
 *   tagsistant_query("SQL statement", return_string, &string); // note the &
 * 
 * @param return_string string pointer cast to void* which holds the string to be returned
 * @param result dbi_result pointer
 * @return 0 (always, due to SQLite policy, may change in the future)
 */
int tagsistant_return_string(void *return_string, dbi_result result)
{
	gchar **result_string = (gchar **) return_string;

	*result_string = dbi_result_get_string_copy_idx(result, 1);

	dbg('s', LOG_INFO, "Returning string: %s", *result_string);

	return (0);
}

/**
 * Creates a (partial) triple tag
 *
 * @param conn dbi_conn reference
 * @param namespace the namespace of the triple tag
 * @param the optional key of the triple tag
 * @param the optional value of the triple tag
 */
void tagsistant_sql_create_tag(dbi_conn conn, const gchar *namespace, const gchar *key, const gchar *value)
{
	if (!namespace) return;

	tagsistant_query(
		"insert into tags(tagname, `key`, value) "
			"values ('%s', '%s', '%s')",
		conn,
		NULL,
		NULL,
		namespace,
		_safe_string(key),
		_safe_string(value));
}

/**
 * Is an object tagged by at least one tag?
 *
 * @param conn dbi_conn reference
 * @param inode the object inode
 * @return true if object is tagged by at least one tag, false otherwise
 */
int tagsistant_object_is_tagged(dbi_conn conn, tagsistant_inode inode)
{
	tagsistant_inode still_exists = 0;

	tagsistant_query(
		"select inode from tagging where inode = %d limit 1",
		conn, tagsistant_return_integer, &still_exists, inode);
	
	return ((still_exists) ? 1 : 0);
}

/**
 * Is an object tagged by a given tag?
 *
 * @param conn dbi_conn reference
 * @param inode the object inode
 * @param tag_id the tag ID
 * @return  true if object is tagged by given tag, false otherwise
 */
int tagsistant_object_is_tagged_as(dbi_conn conn, tagsistant_inode inode, tagsistant_inode tag_id)
{
	tagsistant_inode is_tagged = 0;

	tagsistant_query(
		"select inode from tagging where inode = %d and tag_id = %d limit 1",
		conn, tagsistant_return_integer, &is_tagged, inode, tag_id);
	
	return ((is_tagged) ? 1 : 0);
}

/**
 * Remove all the tags applied to an object
 *
 * @param conn dbi_conn reference
 * @param inode the object inode
 */
void tagsistant_full_untag_object(dbi_conn conn, tagsistant_inode inode)
{
	tagsistant_query("delete from tagging where inode = %d", conn, NULL, NULL, inode);
}

/**
 * Return the id of a tag
 *
 * @param conn dbi_conn reference
 * @param tagname the tag name or the namespace of a triple tag
 * @param key the key of a triple tag
 * @param value the value of a triple tag
 * @return the id of the tag
 */
tagsistant_inode tagsistant_sql_get_tag_id(dbi_conn conn, const gchar *tagname, const gchar *key, const gchar *value)
{
#if TAGSISTANT_ENABLE_TAG_ID_CACHE
	gchar *tag_key = tagsistant_make_tag_key(_safe_string(tagname), _safe_string(key), _safe_string(value));

	// lookup in the cache
	tagsistant_inode *tag_value = (tagsistant_inode *) g_hash_table_lookup(tagsistant_tag_cache, tag_key);

	if (tag_value && *tag_value) {
		g_free(tag_key);
		return (*tag_value);
	}
#endif

	// fetch the tag_id from SQL
	tagsistant_inode tag_id = 0;
	if (value)
		tagsistant_query(
			"select tag_id from tags where tagname = '%s' and `key` = '%s' and value = '%s' limit 1",
			conn, tagsistant_return_integer, &tag_id, tagname, _safe_string(key), _safe_string(value));
	else if (key)
		tagsistant_query(
			"select tag_id from tags where tagname = '%s' and `key` = '%s' limit 1",
			conn, tagsistant_return_integer, &tag_id, tagname, _safe_string(key));
	else
		tagsistant_query(
			"select tag_id from tags where tagname = '%s' limit 1",
			conn, tagsistant_return_integer, &tag_id, tagname);

#if TAGSISTANT_ENABLE_TAG_ID_CACHE
	// save the key and the value in the cache
	if (tag_id) {
		tag_value = g_new0(tagsistant_inode, 1);
		*tag_value = tag_id;
		g_hash_table_insert(tagsistant_tag_cache, tag_key, tag_value);
	}
#endif

	return (tag_id);
}

void tagsistant_remove_tag_from_cache(const gchar *tagname, const gchar *key, const gchar *value)
{
#if TAGSISTANT_ENABLE_TAG_ID_CACHE
	gchar *tag_key = tagsistant_make_tag_key(tagname, _safe_string(key), _safe_string(value));
	g_hash_table_remove(tagsistant_tag_cache, tag_key);
	g_free(tag_key);
#else
	(void) tagname;
#endif
}

/**
 * Deletes a tag
 *
 * @param conn dbi_conn reference
 * @param tagname the name of the tag or the namespace of a triple tag
 * @param key the key of a triple tag
 * @param value the value of a triple tag
 */
void tagsistant_sql_delete_tag(dbi_conn conn, const gchar *tagname, const gchar *key, const gchar *value)
{
	tagsistant_inode tag_id = tagsistant_sql_get_tag_id(conn, tagname, _safe_string(key), _safe_string(value));
	tagsistant_remove_tag_from_cache(tagname, _safe_string(key), _safe_string(value));

	tagsistant_query(
		"delete from tags where tagname = '%s' and `key` = '%s' and value = '%s'",
		conn, NULL, NULL, tagname, _safe_string(key), _safe_string(value));

	tagsistant_query(
		"delete from tagging where tag_id = '%d'",
		conn, NULL, NULL, tag_id);

	tagsistant_query(
		"delete from relations where tag1_id = '%d' or tag2_id = '%d'",
		conn, NULL, NULL, tag_id, tag_id);
}

/**
 * Tag an object
 *
 * @param conn dbi_conn reference
 * @param tagname the tag name
 * @param inode the object inode
 */
void tagsistant_sql_tag_object(
	dbi_conn conn,
	const gchar *tagname,
	const gchar *key,
	const gchar *value,
	tagsistant_inode inode)
{
	const gchar *_key = key ? key : "";
	const gchar *_value = value ? value : "";

	tagsistant_inode tag_id = tagsistant_sql_get_tag_id(conn, tagname, _key, _value);
	if (!tag_id) {
		tagsistant_sql_create_tag(conn, tagname, _key, _value);
		tag_id = tagsistant_sql_get_tag_id(conn, tagname, _key, _value);
	}

	if (value) {
		dbg('s', LOG_INFO, "Tagging object %d as %s:%s=%s (%d)", inode, tagname, _key, _value, tag_id);
	} else {
		dbg('s', LOG_INFO, "Tagging object %d as %s (%d)", inode, tagname, tag_id);
	}

	tagsistant_query("insert into tagging(tag_id, inode) values('%d', '%d')", conn, NULL, NULL, tag_id, inode);
}

/**
 * Untag an object
 *
 * @param conn dbi_conn reference
 * @param tagname the tag name
 * @param inode the object inode
 */
void tagsistant_sql_untag_object(dbi_conn conn, const gchar *tagname, const gchar *key, const gchar *value, tagsistant_inode inode)
{
	tagsistant_inode tag_id = tagsistant_sql_get_tag_id(conn, tagname, _safe_string(key), _safe_string(value));

	if (value) {
		dbg('s', LOG_INFO, "Untagging object %d from tag %s:%s=%s (%d)",
			inode, tagname, _safe_string(key), _safe_string(value), tag_id);
	} else {
		dbg('s', LOG_INFO, "Untagging object %d from tag %s (%d)",
			inode, tagname, tag_id);
	}

	tagsistant_query(
		"delete from tagging where tag_id = '%d' and inode = '%d'",
		conn, NULL, NULL, tag_id, inode);
}

/**
 * Rename a tag
 *
 * @param conn dbi_conn reference
 * @param tagname the new name of the tag
 * @param oldtagname the old name of the tag
 */
void tagsistant_sql_rename_tag(dbi_conn conn, const gchar *tagname, const gchar *oldtagname)
{
	tagsistant_query("update tags set tagname = '%s' where tagname = '%s'", conn, NULL, NULL, tagname, oldtagname);
}

/**
 * Check the existence of an alias
 *
 * @param conn dbi_conn reference
 * @param alias the alias to be looked up in the DB
 * @return 1 if found, 0 otherwise
 */
int tagsistant_sql_alias_exists(dbi_conn conn, const gchar *alias)
{
	int exists = 0;
	tagsistant_query(
		"select 1 from aliases where alias = '%s'",
		conn, tagsistant_return_integer, &exists, alias);
	return (exists);
}

/**
 * Create an alias
 *
 * @param conn dbi_conn reference
 * @param alias the alias to be created
 */
void tagsistant_sql_alias_create(dbi_conn conn, const gchar *alias)
{
	if (tagsistant_sql_alias_exists(conn, alias)) return;
	tagsistant_query(
		"insert into aliases (alias, query) values ('%s', '')",
		conn, NULL, NULL, alias);
}

/**
 * Delete an alias
 *
 * @param conn dbi_conn reference
 * @param alias the alias to be deleted
 */
void tagsistant_sql_alias_delete(dbi_conn conn, const gchar *alias)
{
	tagsistant_query(
		"delete from aliases where alias = '%s'",
		conn, NULL, NULL, alias);
}

/**
 * Set the query bookmarked by an alias
 *
 * @param conn dbi_conn reference
 * @param alias the alias to be set
 * @param query the query bookmarked by the alias
 */
void tagsistant_sql_alias_set(dbi_conn conn, const gchar *alias, const gchar *query)
{
	tagsistant_query(
		"update aliases set query = '%s' where alias = '%s'",
		conn, NULL, NULL, query, alias);
}

/**
 * Get the query bookmarked by an alias
 *
 * @param conn the dbi_conn reference
 * @param alias the alias to be looked up in the DB
 * @return the value as a string (must be freed in the calling function)
 */
gchar *tagsistant_sql_alias_get(dbi_conn conn, const gchar *alias)
{
	gchar *value = NULL;

	tagsistant_query(
		"select query from aliases where alias = '%s'",
		conn, tagsistant_return_string, &value, alias);

	return (value);
}

/**
 * Get the length of an alias
 *
 * @param conn the dbi_conn reference
 * @param alias the alias to be measured
 * @return the length of the alias
 */
size_t tagsistant_sql_alias_get_length(dbi_conn conn, const gchar *alias)
{
	size_t length = 0;

	gchar *value = tagsistant_sql_alias_get(conn, alias);
	length = strlen(value);
	g_free(value);

	return (length);
}
