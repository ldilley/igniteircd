/************************************************************************
 *   IRC - Internet Relay Chat, include/msg.h
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
 * $Id: msg.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */
#ifndef INCLUDED_msg_h
#define INCLUDED_msg_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

struct Client;

/* 
 * Message table structure 
 */
struct  Message
{
  char  *cmd;
  int   (* func)();
  unsigned int  count;                  /* number of times command used */
  int   parameters;
  char  flags;
  /* bit 0 set means that this command is allowed to be used
   * only on the average of once per 2 seconds -SRB */

  /* I could have defined other bit maps to above instead of the next two
     flags that I added. so sue me. -Dianora */

  char    allow_unregistered_use;       /* flag if this command can be
                                           used if unregistered */

  char    reset_idle;                   /* flag if this command causes
                                           idle time to be reset */
  unsigned long bytes;
};

struct MessageTree
{
  char*               final;
  struct Message*     msg;
  struct MessageTree* pointers[26];
}; 

typedef struct MessageTree MESSAGE_TREE;


#define MSG_PRIVATE  "PRIVMSG"  /* PRIV */
#define MSG_WHO      "WHO"      /* WHO  -> WHOC */
#define MSG_WHOIS    "WHOIS"    /* WHOI */
#define MSG_WHOWAS   "WHOWAS"   /* WHOW */
#define MSG_USER     "USER"     /* USER */
#define MSG_NICK     "NICK"     /* NICK */
#define MSG_SERVER   "SERVER"   /* SERV */
#define MSG_LIST     "LIST"     /* LIST */
#define MSG_TOPIC    "TOPIC"    /* TOPI */
#define MSG_INVITE   "INVITE"   /* INVI */
#define MSG_VERSION  "VERSION"  /* VERS */
#define MSG_QUIT     "QUIT"     /* QUIT */
#define MSG_SQUIT    "SQUIT"    /* SQUI */
#define MSG_KILL     "KILL"     /* KILL */
#define MSG_INFO     "INFO"     /* INFO */
#define MSG_LINKS    "LINKS"    /* LINK */
#define MSG_STATS    "STATS"    /* STAT */
#define MSG_USERS    "USERS"    /* USER -> USRS */
#define MSG_HELP     "HELP"     /* HELP */
#define MSG_ERROR    "ERROR"    /* ERRO */
#define MSG_AWAY     "AWAY"     /* AWAY */
#define MSG_CONNECT  "CONNECT"  /* CONN */
#define MSG_PING     "PING"     /* PING */
#define MSG_PONG     "PONG"     /* PONG */
#define MSG_OPER     "OPER"     /* OPER */
#define MSG_PASS     "PASS"     /* PASS */
#define MSG_WALLOPS  "WALLOPS"  /* WALL */
#define MSG_TIME     "TIME"     /* TIME */
#define MSG_NAMES    "NAMES"    /* NAME */
#define MSG_ADMIN    "ADMIN"    /* ADMI */
#define MSG_TRACE    "TRACE"    /* TRAC */
#define MSG_LTRACE   "LTRACE"   /* LTRA */
#define MSG_NOTICE   "NOTICE"   /* NOTI */
#define MSG_JOIN     "JOIN"     /* JOIN */
#define MSG_PART     "PART"     /* PART */
#define MSG_LUSERS   "LUSERS"   /* LUSE */
#define MSG_MOTD     "MOTD"     /* MOTD */
#define MSG_MODE     "MODE"     /* MODE */
#define MSG_KICK     "KICK"     /* KICK */
#define MSG_USERHOST "USERHOST" /* USER -> USRH */
#define MSG_ISON     "ISON"     /* ISON */
#define MSG_REHASH   "REHASH"   /* REHA */
#define MSG_RESTART  "RESTART"  /* REST */
#define MSG_CLOSE    "CLOSE"    /* CLOS */
#define MSG_SVINFO   "SVINFO"   /* SVINFO */
#define MSG_SJOIN    "SJOIN"    /* SJOIN */
#define MSG_CAPAB    "CAPAB"    /* CAPAB */
#define MSG_DIE      "DIE"      /* DIE */
#define MSG_HASH     "HASH"     /* HASH */
#define MSG_DNS      "DNS"      /* DNS  -> DNSS */
#define MSG_OPERWALL "OPERWALL" /* OPERWALL */
#define MSG_KLINE    "KLINE"    /* KLINE */
#define MSG_UNKLINE  "UNKLINE"  /* UNKLINE */
#define MSG_DLINE    "DLINE"    /* DLINE */
#define MSG_UNDLINE  "UNDLINE"  /* UNDLINE */
#define MSG_HTM      "HTM"      /* HTM */
#define MSG_SET      "SET"      /* SET */

#define MSG_GLINE    "GLINE"    /* GLINE */
#define MSG_UNGLINE  "UNGLINE"  /* UNGLINE */

#define MSG_LOCOPS   "LOCOPS"   /* LOCOPS */
#ifdef LWALLOPS
#define MSG_LWALLOPS "LWALLOPS" /* Same as LOCOPS */
#endif /* LWALLOPS */

#define MSG_KNOCK          "KNOCK"  /* KNOCK */

#define MAXPARA    15 

#define MSG_TESTLINE "TESTLINE"

#define MSG_FJOIN "FJOIN"
#define MSG_FNICK "FNICK"

#ifdef MSGTAB
#ifndef INCLUDED_m_commands_h
#include "m_commands.h"       /* m_xxx */
#endif
struct Message msgtab[] = {
#ifdef IDLE_FROM_MSG    /* reset idle time only if privmsg used */
#ifdef IDLE_CHECK       /* reset idle time only if valid target for privmsg
                           and target is not source */

  /*                                        |-- allow use even when unreg.
                                            v   yes/no                  */
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 0, 0L },
#else
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 1, 0L },
#endif

  /*                                           ^
                                               |__ reset idle time when 1 */
#else   /* IDLE_FROM_MSG */
#ifdef  IDLE_CHECK      /* reset idle time on anything but privmsg */
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 1, 0L },
#else
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 0, 0L },
  /*                                           ^
                                               |__ reset idle time when 0 */
#endif  /* IDLE_CHECK */
#endif  /* IDLE_FROM_MSG */

  { MSG_NICK,    m_nick,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_NOTICE,  m_notice,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_JOIN,    m_join,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_MODE,    m_mode,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_QUIT,    m_quit,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_PART,    m_part,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KNOCK,   m_knock,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_TOPIC,   m_topic,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_INVITE,  m_invite,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KICK,    m_kick,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WALLOPS, m_wallops,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LOCOPS,  m_locops,   0, MAXPARA, 1, 0, 0, 0L },
#ifdef LWALLOPS
  { MSG_LWALLOPS,m_locops,   0, MAXPARA, 1, 0, 0, 0L },
#endif /* LWALLOPS */

#ifdef IDLE_FROM_MSG

  /* Only m_private has reset idle flag set */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 0, 0L },

#else

  /* else for IDLE_FROM_MSG */
  /* reset idle flag sense is reversed, only reset idle time
   * when its 0, for IDLE_FROM_MSG ping/pong do not reset idle time
   */

  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 0, 1, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 1, 0L },

#endif  /* IDLE_FROM_MSG */

  { MSG_ERROR,   m_error,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_KILL,    m_kill,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_USER,    m_user,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_AWAY,    m_away,     0, MAXPARA, 1, 0, 0, 0L },
#ifdef IDLE_FROM_MSG
  { MSG_ISON,    m_ison,     0, 1,       1, 0, 0, 0L },
#else
  /* ISON should not reset idle time ever
   * remember idle flag sense is reversed when IDLE_FROM_MSG is undefined
   */
  { MSG_ISON,    m_ison,     0, 1,       1, 0, 1, 0L },
#endif /* !IDLE_FROM_MSG */
  { MSG_SERVER,  m_server,   0, MAXPARA, 1, 1, 0, 0L },
  { MSG_SQUIT,   m_squit,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHOIS,   m_whois,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHO,     m_who,      0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHOWAS,  m_whowas,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LIST,    m_list,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_NAMES,   m_names,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_USERHOST,m_userhost, 0, 1,       1, 0, 0, 0L },
  { MSG_TRACE,   m_trace,    0, MAXPARA, 1, 0, 0, 0L },
#ifdef LTRACE
  { MSG_LTRACE,  m_ltrace,   0, MAXPARA, 1, 0, 0, 0L },
#endif /* LTRACE */
  { MSG_PASS,    m_pass,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_LUSERS,  m_lusers,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_TIME,    m_time,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_OPER,    m_oper,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CONNECT, m_connect,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_VERSION, m_version,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_STATS,   m_stats,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LINKS,   m_links,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_ADMIN,   m_admin,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_USERS,   m_users,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_HELP,    m_help,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_INFO,    m_info,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_MOTD,    m_motd,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_SVINFO,  m_svinfo,   0, MAXPARA, 1, 1, 0, 0L },
  { MSG_SJOIN,   m_sjoin,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CAPAB,   m_capab,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_OPERWALL, m_operwall,0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CLOSE,   m_close,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KLINE,   m_kline,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_UNKLINE, m_unkline,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_DLINE,   m_dline,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_UNDLINE, m_undline,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_GLINE,   m_gline,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_UNGLINE, m_ungline,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_HASH,    m_hash,     0, MAXPARA, 1, 0, 0, 0L },
#if notyet
  { MSG_DNS,     m_dns,      0, MAXPARA, 1, 0, 0, 0L },
#endif
  { MSG_REHASH,  m_rehash,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_RESTART, m_restart,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_DIE, m_die,          0, MAXPARA, 1, 0, 0, 0L },
  { MSG_HTM,    m_htm,       0, MAXPARA, 1, 0, 0, 0L },
  { MSG_SET,    m_set,       0, MAXPARA, 1, 0, 0, 0L },
  { MSG_TESTLINE,       m_testline,          0, MAXPARA, 1, 0, 0, 0L },
  { MSG_FJOIN,  m_fjoin,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_FNICK,  m_fnick,     0, MAXPARA, 1, 0, 0, 0L },
  { (char *) 0, (int (*)()) 0 , 0, 0,    0, 0, 0, 0L }
};

struct MessageTree* msg_tree_root;

#else
extern struct Message       msgtab[];
extern struct MessageTree*  msg_tree_root;
#endif

#endif /* INCLUDED_msg_h */
