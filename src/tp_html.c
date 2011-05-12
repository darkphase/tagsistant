/*
   Tagsistant (tagfs) -- tp_html.c
   Copyright (C) 2006-2009 Tx0 <tx0@strumentiresistenti.org>

   Tagsistant html plugin which makes decisions on file MIME types.

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* declaring mime type */
char mime_type[] = "text/html";

/* flags used by regex compiler */
#define _RX_COMPILE_FLAGS G_REGEX_CASELESS|G_REGEX_MULTILINE|G_REGEX_DOTALL|G_REGEX_EXTENDED|G_REGEX_OPTIMIZE

/* static mutex used by processor */
GStaticMutex processor_mutex = G_STATIC_MUTEX_INIT;

GRegex *_rx_title = NULL;

/* exported init function */
int plugin_init()
{
	/* intialize regular expressions */
	_rx_title = g_regex_new("<title>([^<]+)</title>", _RX_COMPILE_FLAGS, 0, NULL);

	return 1;
}


/* exported processor function */
int processor(const tagsistant_querytree_t *qtree)
{
	/* default tagging */
	sql_tag_object("document", qtree->object_id);
	sql_tag_object("webpage", qtree->object_id);
	sql_tag_object("html", qtree->object_id);

	/* apply regular expressions to document content */

	/* 1. open document */
	int fd = open(qtree->full_archive_path, 0);
	if (-1 == fd) {
		int error = errno;
		dbg(LOG_ERROR, "Unable to open %s: %s", qtree->full_archive_path, strerror(error));
		return TP_ERROR;
	}

	/* 2. lock the mutex */
	g_static_mutex_lock(&processor_mutex);

	/* 3. read 65K of document content */
	char buf[65535];
	int r = read(fd, buf, 65535);
	(void) r;

	/* 4. look for matching portions */
	GMatchInfo *match_info;
	g_regex_match(_rx_title, buf, 0, &match_info);

	/* 5. unlock the mutex */
	g_static_mutex_unlock(&processor_mutex);

	/* 6. process all the matched portions */
	while (g_match_info_matches(match_info)) {
		gchar *title = g_match_info_fetch(match_info, 1);
		dbg(LOG_INFO, "Found title: %s", title);

		gchar **tokens = g_strsplit_set(title, " \t,.!?", 255);
		g_free(title);

		int x = 0;
		while (tokens[x]) {
			if (strlen(tokens[x]) >= 3) sql_tag_object(tokens[x], qtree->object_id);
			x++;
		}

		g_strfreev(tokens);

		g_match_info_next(match_info, NULL);
	}

	return TP_STOP;
}

/* exported finalize function */
void plugin_free()
{
	/* unreference regular expressions */
	g_regex_unref(_rx_title);
}

/* vim:ts=4:autoindent:nocindent:syntax=c */
