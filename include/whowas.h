/************************************************************************
 *   IRC - Internet Relay Chat, include/whowas.h
 *   Copyright (C) 1990  Markku Savela
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
 */

/*
 * $Id: whowas.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */
#ifndef INCLUDED_whowas_h
#define INCLUDED_whowas_h
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

/*
 * Whowas hash table size
 *
 */
#define WW_MAX 65536

struct User;
struct CLient;

/*
** WHOWAS structure moved here from whowas.c
*/
typedef struct aname {
  struct User  *ww_user;
  struct Client *ww_online;
  time_t  ww_logout;
  char    ww_nick[NICKLEN+1];
  char    ww_info[REALLEN+1];
} aName;

/*
  lets speed this up...
  also removed away information. *tough*
  - Dianora
 */
typedef struct Whowas
{
  int  hashv;
  char name[NICKLEN + 1];
  char username[USERLEN + 1]; 
  char hostname[HOSTLEN + 1];
  const char* servername;
  char realname[REALLEN + 1];
  time_t logoff;
  struct Client *online; /* Pointer to new nickname for chasing or NULL */
  struct Whowas *next;  /* for hash table... */
  struct Whowas *prev;  /* for hash table... */
  struct Whowas *cnext; /* for client struct linked list */
  struct Whowas *cprev; /* for client struct linked list */
}aWhowas;

/*
** initwhowas
*/
extern void initwhowas(void);

/*
** add_history
**      Add the currently defined name of the client to history.
**      usually called before changing to a new name (nick).
**      Client must be a fully registered user (specifically,
**      the user structure must have been allocated).
*/
void    add_history (struct Client *, int);

/*
** off_history
**      This must be called when the client structure is about to
**      be released. History mechanism keeps pointers to client
**      structures and it must know when they cease to exist. This
**      also implicitly calls AddHistory.
*/
void    off_history (struct Client *);

/*
** get_history
**      Return the current client that was using the given
**      nickname within the timelimit. Returns NULL, if no
**      one found...
*/
struct Client *get_history (char *, time_t);
                                        /* Nick name */
                                        /* Time limit in seconds */

int     m_whowas (struct Client *, struct Client *, int, char *[]);

/*
** for debugging...counts related structures stored in whowas array.
*/
void    count_whowas_memory (int *, u_long *);

#endif /* INCLUDED_whowas_h */
