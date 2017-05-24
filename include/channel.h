/************************************************************************
 *
 *   IRC - Internet Relay Chat, include/channel.h
 *   Copyright (C) 1990 Jarkko Oikarinen
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
 * $Id: channel.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 */

#ifndef INCLUDED_channel_h
#define INCLUDED_channel_h
#ifndef INCLUDED_config_h
#include "config.h"           /* config settings */
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"        /* buffer sizes */
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>        /* time_t */
#define INCLUDED_sys_types_h
#endif

struct SLink;
struct Client;


/* mode structure for channels */

struct Mode
{
  unsigned int  mode;
  int   limit;
  char  key[KEYLEN + 1];
};

/* channel structure */

struct Channel
{
  struct Channel* nextch;
  struct Channel* prevch;
  struct Channel* hnextch;
  struct Mode     mode;
  char            topic[TOPICLEN + 1];
#ifdef TOPIC_INFO
  char            topic_nick[NICKLEN + 1];
  time_t          topic_time;
#endif
  int             users;
  struct SLink*   members;
  struct SLink*   invites;
  struct SLink*   banlist;
  struct SLink*   exceptlist;
  int             num_bed;  /* number of bans+exceptions+denies */
#ifdef JUPE_CHANNEL
  int		  juped;
#endif  
  time_t          channelts;
#ifdef USE_KNOCK
  time_t          last_knock;
#endif
#ifdef FLUD
  time_t          fludblock;
  struct fludbot* fluders;
#endif
  char            chname[1];
};

typedef struct  Channel aChannel;

extern  struct  Channel *channel;

#define CREATE 1        /* whether a channel should be
                           created or just tested for existance */

#define MODEBUFLEN      200

#define NullChn ((aChannel *)0)

#define ChannelExists(n)        (hash_find_channel(n, NullChn) != NullChn)

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS   5

extern struct Channel* find_channel (char *, struct Channel *);
extern struct SLink*   find_channel_link(struct SLink *, struct Channel *);
extern void    remove_user_from_channel(struct Client *,struct Channel *,int);
extern void    del_invite (struct Client *, struct Channel *);
extern void    send_user_joins (struct Client *, struct Client *);
extern int     can_send (struct Client *, struct Channel *);
extern int     is_chan_op (struct Client *, struct Channel *);
extern int     is_deopped (struct Client *, struct Channel *);
extern int     is_admin  (struct Client *, struct Channel *);
extern int     has_voice (struct Client *, struct Channel *);
extern int     user_channel_mode(struct Client *, struct Channel *);
extern int     count_channels (struct Client *);
extern int     m_names(struct Client *, struct Client *,int, char **);
extern void    send_channel_modes (struct Client *, struct Channel *);
extern void    del_invite (struct Client *, struct Channel *);
extern int     check_channel_name(const char* name);
extern void    channel_modes(struct Client *, char *, char *, struct Channel*);
extern void    set_channel_mode(struct Client *, struct Client *, 
                                struct Channel *, int, char **);
#ifdef JUPE_CHANNEL
extern void report_juped_channels(struct Client *);
#endif

extern int total_hackops;
extern int total_ignoreops;

/* this should eliminate a lot of ifdef's in the main code... -orabidoo */
#ifdef BAN_INFO
#  define BANSTR(l)  ((l)->value.banptr->banstr)
#else
#  define BANSTR(l)  ((l)->value.cp)
#endif

/* Channel related flags */

#define CHFL_CHANOP     0x0001 /* Channel operator */
#define CHFL_VOICE      0x0002 /* the power to speak */
#define CHFL_DEOPPED    0x0004 /* deopped by us, modes need to be bounced */
#define CHFL_BAN        0x0008 /* banned */
#define CHFL_EXCEPTION  0x0010 /* exception to ban channel flag */
#define CHFL_DENY       0x0020 /* regular expression deny flag */
/* #define CHFL_ADMIN      0x0080 */ /* Channel Administrator */

/* Channel Visibility macros */

#define MODE_CHANOP     CHFL_CHANOP
#define MODE_VOICE      CHFL_VOICE
#define MODE_DEOPPED    CHFL_DEOPPED
/* #define MODE_ADMIN      CHFL_ADMIN */
#define MODE_PRIVATE    0x0008
#define MODE_SECRET     0x0010
#define MODE_MODERATED  0x0020
#define MODE_TOPICLIMIT 0x0040
#define MODE_INVITEONLY 0x0080
#define MODE_NOPRIVMSGS 0x0100
#define MODE_KEY        0x0200
#define MODE_BAN        0x0400
#define MODE_EXCEPTION  0x0800
#define MODE_DENY       0x1000
#define MODE_LIMIT      0x2000  /* was 0x1000 */
#define MODE_FLAGS      0x2fff  /* was 0x1fff */

#ifdef SERVICES
#define MODE_REGISTERED 0x4000
#endif /* SERVICES */

#ifdef NEED_SPLITCODE

extern int server_was_split;
extern time_t server_split_time;

#ifdef SPLIT_PONG
extern int got_server_pong;
#endif /* SPLIT_PONG */

#endif /* NEED_SPLITCODE */

/*
 * mode flags which take another parameter (With PARAmeterS)
 */
/* #define MODE_WPARAS (MODE_ADMIN|MODE_CHANOP|MODE_VOICE|MODE_BAN|\
                     MODE_EXCEPTION|MODE_DENY|MODE_KEY|MODE_LIMIT) */

#define MODE_WPARAS (MODE_CHANOP|MODE_VOICE|MODE_BAN|\
                     MODE_EXCEPTION|MODE_DENY|MODE_KEY|MODE_LIMIT)

/*
 * Undefined here, these are used in conjunction with the above modes in
 * the source.
#define MODE_QUERY      0x10000000
#define MODE_DEL       0x40000000
#define MODE_ADD       0x80000000
 */

/* used in SetMode() in channel.c and m_umode() in s_msg.c */

#define MODE_NULL      0
#define MODE_QUERY     0x10000000
#define MODE_ADD       0x40000000
#define MODE_DEL       0x20000000

#define HoldChannel(x)          (!(x))
/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_PRIVATE))
/* channel visible */
#define ShowChannel(v,c)        (PubChannel(c) || IsMember((v),(c)))
#define PubChannel(x)           ((!x) || ((x)->mode.mode &\
                                 (MODE_PRIVATE | MODE_SECRET)) == 0)

#define IsMember(blah,chan) ((blah && blah->user && \
                find_channel_link((blah->user)->channel, chan)) ? 1 : 0)

#define IsChannelName(name) ((name) && (*(name) == '#' || *(name) == '&'))

#ifdef BAN_INFO
/*
  Move BAN_INFO information out of the SLink struct
  its _only_ used for bans, no use wasting the memory for it
  in any other type of link. Keep in mind, doing this that
  it makes it slower as more Malloc's/Free's have to be done, 
  on the plus side bans are a smaller percentage of SLink usage.
  Over all, the th+hybrid coding team came to the conclusion
  it was worth the effort.

  - Dianora
*/
typedef struct Ban      /* also used for exceptions -orabidoo */
{
  char *banstr;
  char *who;
  time_t when;
} aBan;
#endif

#ifdef NEED_SPLITCODE

extern int server_was_split;
#if defined(SPLIT_PONG)
extern int got_server_pong;
#endif

#endif  /* NEED_SPLITCODE */

#endif  /* INCLUDED_channel_h */

