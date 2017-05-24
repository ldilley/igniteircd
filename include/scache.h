/************************************************************************
 *   IRC - Internet Relay Chat, include/scache.h
 *   Copyright (C) 1992 Darren Reed
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * "scache.h". - Headers file.
 *
 * $Id: scache.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 *
 */
#ifndef INCLUDED_scache_h
#define INCLUDED_scache_h

extern void        clear_scache_hash_table(void);
extern const char* find_or_add(const char* name);
extern void        count_scache(int *,unsigned long *);
extern void        list_scache(struct Client *, struct Client *,int, char **);

#endif
