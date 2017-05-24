/* 
 *   Internet Relay Chat, include/s_stats.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 * $Id: s_stats.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */
#ifndef INCLUDED_s_stats_h
#define INCLUDED_s_stats_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;

/*
 * statistics structures
 */
struct  ServerStatistics {
  unsigned int    is_cl;  /* number of client connections */
  unsigned int    is_sv;  /* number of server connections */
  unsigned int    is_ni;  /* connection but no idea who it was */
  unsigned short  is_cbs; /* bytes sent to clients */
  unsigned short  is_cbr; /* bytes received to clients */
  unsigned short  is_sbs; /* bytes sent to servers */
  unsigned short  is_sbr; /* bytes received to servers */
  unsigned long   is_cks; /* k-bytes sent to clients */
  unsigned long   is_ckr; /* k-bytes received to clients */
  unsigned long   is_sks; /* k-bytes sent to servers */
  unsigned long   is_skr; /* k-bytes received to servers */
  time_t          is_cti; /* time spent connected by clients */
  time_t          is_sti; /* time spent connected by servers */
  unsigned int    is_ac;  /* connections accepted */
  unsigned int    is_ref; /* accepts refused */
  unsigned int    is_unco; /* unknown commands */
  unsigned int    is_wrdi; /* command going in wrong direction */
  unsigned int    is_unpf; /* unknown prefix */
  unsigned int    is_empt; /* empty message */
  unsigned int    is_num; /* numeric message */
  unsigned int    is_kill; /* number of kills generated on collisions */
  unsigned int    is_fake; /* MODE 'fakes' */
  unsigned int    is_asuc; /* successful auth requests */
  unsigned int    is_abad; /* bad auth requests */
  unsigned int    is_udp; /* packets recv'd on udp port */
  unsigned int    is_loc; /* local connections made */
#ifdef FLUD
  unsigned int    is_flud;        /* users/channels flood protected */
#endif /* FLUD */
};

extern struct ServerStatistics* ServerStats;

extern void init_stats(void);
extern void tstats(struct Client* client, const char* name);

#endif /* INCLUDED_s_stats_h */
