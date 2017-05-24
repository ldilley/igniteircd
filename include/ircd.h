/************************************************************************
 *   IRC - Internet Relay Chat, include/ircd.h
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
 * "ircd.h". - Headers file.
 *
 * $Id: ircd.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 *
 */
#ifndef INCLUDED_ircd_h
#define INCLUDED_ircd_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;

struct SetOptions
{
  int maxclients;       /* max clients allowed */
  int autoconn;         /* autoconn enabled for all servers? */
  int noisy_htm;        /* noisy htm or not ? */
  int lifesux;

#ifdef IDLE_CHECK
  int idletime;
#endif

#ifdef FLUD
  int fludnum;
  int fludtime;
  int fludblock;
#endif

#ifdef ANTI_DRONE_FLOOD
  int dronetime;
  int dronecount;
#endif

#ifdef NEED_SPLITCODE
  time_t server_split_recovery_time;
  int split_smallnet_size;
  int split_smallnet_users;
#endif

#ifdef ANTI_SPAMBOT
  int spam_num;
  int spam_time;
#endif
};

struct Counter {
  int     server;         /* servers */
  int     myserver;       /* my servers */
  int     oper;           /* Opers */
  int     chan;           /* Channels */
  int     local;          /* Local Clients */
  int     total;          /* total clients */
  int     invisi;         /* invisible clients */
  int     unknown;        /* unknown connections */
  int     max_loc;        /* MAX local clients */
  int     max_tot;        /* MAX global clients */
  unsigned long totalrestartcount;   /* Total clients since restart */
};

extern struct SetOptions GlobalSetOptions;  /* defined in ircd.c */
/*
 * XXX - ACK!!!
 */
/*
 * ZZZ - These can go away slowly as they are rewritten.
 * calm down Tom.
 * heh :) --Bleep
 *
 */
#define AUTOCONN   GlobalSetOptions.autoconn
#define DRONECOUNT GlobalSetOptions.dronecount
#define DRONETIME  GlobalSetOptions.dronetime
#define FLUDBLOCK  GlobalSetOptions.fludblock
#define FLUDNUM    GlobalSetOptions.fludnum
#define FLUDTIME   GlobalSetOptions.fludtime
#define IDLETIME   GlobalSetOptions.idletime
#define LIFESUX    GlobalSetOptions.lifesux
#define MAXCLIENTS GlobalSetOptions.maxclients
#define NOISYHTM   GlobalSetOptions.noisy_htm
#define SPAMNUM    GlobalSetOptions.spam_num
#define SPAMTIME   GlobalSetOptions.spam_time
#define SPLITDELAY GlobalSetOptions.server_split_recovery_time
#define SPLITNUM   GlobalSetOptions.split_smallnet_size
#define SPLITUSERS GlobalSetOptions.split_smallnet_users


extern char*          debugmode;
extern int            debuglevel;
extern int            debugtty;
extern char*          creation;
extern char*          generation;
extern char*          platform;
extern char*          infotext[];
extern char*          serno;
extern char*          ircd_version;
extern const char     serveropts[];
extern int            LRV;
extern int            cold_start;
extern int            dline_in_progress;
extern int            dorehash;
extern int            rehashed;
extern float          currlife;
extern struct Client  me;
extern struct Client* GlobalClientList;
extern struct Client* local[];
extern struct Counter Count;
extern time_t         CurrentTime;
extern time_t         LCF;
extern time_t         nextconnect;
extern time_t         nextping;

char *isupport;

extern struct Client* local_cptr_list;
extern struct Client* oper_cptr_list;
extern struct Client* serv_cptr_list;

#ifdef REJECT_HOLD
extern int reject_held_fds;
#endif

extern size_t   get_maxrss(void);

/* 1800 == half an hour
 * if clock set back more than this length of time
 * complain
 */
#define MAX_SETBACK_TIME 1800
#endif
