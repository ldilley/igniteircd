/************************************************************************
 *   IRC - Internet Relay Chat, include/s_user.h
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
 * "s_user.h". - Headers file.
 *
 * $Id: s_user.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 *
 */
#ifndef INCLUDED_s_user_h
#define INCLUDED_s_user_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>      /* time_t */
#define INCLUDED_sys_types_h
#endif

struct Client;

extern int   user_mode(struct Client *, struct Client *, int, char **);
extern void  send_umode (struct Client *, struct Client *,
                         int, int, char *);
extern void  send_umode_out (struct Client*, struct Client *, int);
extern int   show_lusers(struct Client *, struct Client *, int, char **);
extern void  show_opers(struct Client* client);


#endif
