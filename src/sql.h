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

/* execute SQL query adding file:line coords */
#define  do_sql(dbh, statement, callback, firstarg)\
	real_do_sql(dbh, statement, callback, firstarg, __FILE__, (unsigned int) __LINE__)

/* real function to execute SQL statements */
extern int real_do_sql(sqlite3 **dbh, char *statement, int (*callback)(void *, int, char **, char **), void *firstarg, char *file, unsigned int line);

/* execute SQL statements autoformatting the SQL string and adding file:line coords */
#define tagsistant_query(format, callback, firstarg, ...) _tagsistant_query(format, __FILE__, __LINE__, callback, firstarg, ## __VA_ARGS__)

/* the real code behind the previous macro */
extern int _tagsistant_query(const char *format, gchar *file, int line, int (*callback)(void *, int, char **, char **), void *firstarg, ...);

extern int return_integer(void *return_integer, int argc, char **argv, char **azColName);
extern int return_string(void *return_string, int argc, char **argv, char **azColName);
extern int get_exact_tag_id(const gchar *tagname);

extern int report_if_exists(void *exists_buffer, int argc, char **argv, char **azColName);
extern gboolean sql_tag_exists(const gchar* tagname);

extern tagsistant_id sql_create_object(const gchar *basename, const gchar *path);

extern gchar *sql_objectpath(tagsistant_id object_id);
extern void sql_delete_object(tagsistant_id object_id);

/***************\
 * SQL QUERIES *
\***************/

#define tagsistant_init_database() {\
	tagsistant_query("create table tags (tag_id integer primary key autoincrement not null, tagname varchar(65) unique not null);", NULL, NULL);\
	tagsistant_query("create table objects (object_id integer not null primary key autoincrement, objectname text(255) not null, path text(1024) unique not null);", NULL, NULL);\
	tagsistant_query("create table tagging (object_id integer not null primary key autoincrement, tag_id not null, constraint Tagging_key unique (object_id, tag_id));", NULL, NULL);\
	tagsistant_query("create table relations(relation_id integer primary key autoincrement not null, tag1_id integer not null, relation varchar not null, tag2_id integer not null);", NULL, NULL);\
	tagsistant_query("create index tags_index on tagging (object_id, tag_id);", NULL, NULL);\
	tagsistant_query("create index relations_index on relations (tag1_id, tag2_id);", NULL, NULL);\
	tagsistant_query("create index relations_type_index on relations (relation);", NULL, NULL);\
}

#define sql_create_tag(tagname) tagsistant_query("insert into tags(tagname) values(\"%s\");", NULL, NULL, tagname)

#define sql_get_tag_id(tagname, tag_id) tagsistant_query("select tag_id from tags where tagname = \"%s\"", return_integer, &tag_id, tagname);\

#define sql_delete_tag(tagname) {\
	int tag_id = 0;\
	sql_get_tag_id(tagname, tag_id);\
	tagsistant_query("delete from tags where tagname = \"%s\";", NULL, NULL, tagname);\
	tagsistant_query("delete from tagging where tag_id = \"%d\";", NULL, NULL, tag_id);\
	tagsistant_query("delete from relations where tag1_id = \"%d\" or tag2_id = \"%d\";", NULL, NULL, tag_id, tag_id);\
}

#define sql_tag_object(tagname, object_id) {\
	int tag_id = 0;\
	tagsistant_query("insert into tags(tagname) values(\"%s\");", NULL, NULL, tagname);\
	sql_get_tag_id(tagname, tag_id);\
	tagsistant_query("insert into tagging(tag_id, object_id) values(\"%d\", \"%d\");", NULL, NULL, tag_id, object_id);\
}

#define sql_untag_object(tagname, object_id) {\
	int tag_id = 0;\
	sql_get_tag_id(tagname, tag_id);\
	tagsistant_query("delete from tagging where tag_id = \"%d\" and object_id = \"%d\";", NULL, NULL, tag_id, object_id);\
}

#define sql_rename_tag(tagname, oldtagname)\
	tagsistant_query("update tags set tagname = \"%s\" where tagname = \"%s\";", NULL, NULL, tagname, oldtagname);\

#define sql_rename_object(object_id, newname)\
	tagsistant_query("update objects set objectname = \"%s\" where object_id = %d", NULL, NULL, newname, object_id);

#define ALL_FILES_TAGGED		"select objectname from objects join tagging on tagging.object_id = objects.id where tagging.tagname = \"%s\""

#define tag_object(object_id, tagname) sql_tag_object(tagname, object_id)
#define untag_object(object_id, tagname) sql_untag_object(tagname, object_id)
