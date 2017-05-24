/************************************************************************
 *
 *   IRC - Internet Relay Chat, include/flud.h
 *   Copyright (C) 1999 Michael Pearce
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
 * $Id: flud.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $ 
 */
#ifndef INCLUDED_flud_h
#define INCLUDED_flud_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif


#ifdef FLUD
struct SLink;
struct Client;
struct Channel;
struct BlockHeap;

struct fludbot {
        struct Client   *fluder;
        int             count;
        time_t          first_msg;
        time_t          last_msg;
        struct fludbot  *next;
};

extern struct BlockHeap *free_fludbots;
extern struct BlockHeap *free_Links;

extern void announce_fluder(struct Client *,struct Client *,struct Channel *,int );
extern struct fludbot *remove_fluder_reference(struct fludbot **,
                                                        struct Client *);

extern struct SLink *remove_fludee_reference(struct SLink **,void *);
extern int check_for_ctcp(char *);
extern int check_for_flud(struct Client *,struct Client *,struct Channel *,int);
extern void free_fluders(struct Client *,struct Channel *);
extern void free_fludees(struct Client *);
#endif

#endif /* INCLUDED_flud_h */
