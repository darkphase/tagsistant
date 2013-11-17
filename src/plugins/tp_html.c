/*
   Tagsistant (tagfs) -- tp_html.c
   Copyright (C) 2006-2013 Tx0 <tx0@strumentiresistenti.org>

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

#include "../tagsistant.h"

/* declaring mime type */
char mime_type[] = "text/html";

/* regex */
static GRegex *rx = NULL;

/* exported init function */
int tagsistant_plugin_init()
{
	/* initialize regular expressions */
	gchar *pattern = tagsistant_get_ini_entry("mime:image/html", "filter");
	if (!pattern) pattern = "^(author|date|language)$";

	rx = g_regex_new(pattern, TAGSISTANT_RX_COMPILE_FLAGS, 0, NULL);

	return(1);
}

/* exported processor function */
int tagsistant_processor(tagsistant_querytree *qtree, tagsistant_keyword keywords[TAGSISTANT_MAX_KEYWORDS])
{
	/* default tagging */
	tagsistant_sql_tag_object(qtree->dbi, "document", NULL, NULL, qtree->inode);
	tagsistant_sql_tag_object(qtree->dbi, "webpage", NULL, NULL, qtree->inode);
	tagsistant_sql_tag_object(qtree->dbi, "html", NULL, NULL, qtree->inode);

	/* apply regular expressions to document content */
	tagsistant_plugin_iterator(qtree, keywords, rx);

	return(TP_STOP);
}

/* exported finalize function */
void tagsistant_plugin_free()
{
	/* unreference regular expressions */
	g_regex_unref(rx);
}

/* vim:ts=4:autoindent:nocindent:syntax=c */
