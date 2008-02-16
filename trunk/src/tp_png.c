/*
   Tagsistant (tagfs) -- tp_png.c
   Copyright (C) 2006-2007 Tx0 <tx0@strumentiresistenti.org>

   Tagsistant png plugin which makes decisions on file MIME types.

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
#define DEFAULT_TAG "image"

/* declaring mime type */
char mime_type[] = "image/png";

/* exported init function */
int plugin_init()
{
	return 1;
}

/* exported processor function */
int processor(const char *filename)
{
	dbg(LOG_INFO, "Taggings %s as %s", filename, DEFAULT_TAG);
	tag_file(filename, DEFAULT_TAG);
	return TP_STOP;
}

/* exported finalize function */
void plugin_free()
{
}

// vim:ts=4:autoindent:nocindent:syntax=c