/************************************************************************
 *   IRC - Internet Relay Chat, include/parse.h
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
 * "parse.h". - Headers file.
 *
 *
 * $Id: parse.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 *
 */
#ifndef INCLUDED_parse_h_h
#define INCLUDED_parse_h_h

struct Message;
struct Client;

extern  int     parse (struct Client *, char *, char *);
extern  void    init_tree_parse (struct Message *);
#endif /* INCLUDED_parse_h_h */
