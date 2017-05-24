/* - Internet Relay Chat, include/client.h
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
 * $Id: client.h,v 1.2 2007/02/14 10:00:42 malign Exp $
 */
#ifndef INCLUDED_client_h
#define INCLUDED_client_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#if !defined(CONFIG_H_LEVEL_6_3)
#error Incorrect config.h for this revision of ircd.
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* time_t */
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>      /* in_addr */
#define INCLUDED_netinet_in_h
#endif
#if defined(HAVE_STDDEF_H)
# ifndef INCLUDED_stddef_h
#  include <stddef.h>        /* offsetof */
#  define INCLUDED_stddef_h
# endif
#endif
#ifndef INCLUDED_ircd_defs_h
# include "ircd_defs.h"
#endif
#ifndef INCLUDED_dbuf_h
#include "dbuf.h"
#endif

#define HOSTIPLEN       16      /* Length of dotted quad form of IP        */
                                /* - Dianora                               */
#define PASSWDLEN       20
#define IDLEN           12      /* this is the maximum length, not the actual
                                   generated length; DO NOT CHANGE! */
#define CLIENT_BUFSIZE 512      /* must be at least 512 bytes */

/*
 * pre declare structs
 */
struct SLink;
struct ConfItem;
struct Whowas;
struct fludbot;
struct Zdata;
struct DNSReply;
struct Listener;
struct Client;

int clean_nick_name(char* nick);

/*
 * Client structures
 */
struct User
{
  struct User*   next;          /* chain of anUser structures */
  struct SLink*  channel;       /* chain of channel pointer blocks */
  struct SLink*  invited;       /* chain of invite pointer blocks */
  char*          away;          /* pointer to away message */
  time_t         last;
  int            refcnt;        /* Number of times this block is referenced */
  int            joined;        /* number of channels joined */
  char           id[IDLEN + 1]; /* for future use *hint* */
  const char*    server;        /* pointer to scached server name */
  /*
  ** In a perfect world the 'server' name
  ** should not be needed, a pointer to the
  ** client describing the server is enough.
  ** Unfortunately, in reality, server may
  ** not yet be in links while USER is
  ** introduced... --msa
  */
  /* with auto-removal of dependent links, this may no longer be the
  ** case, but it's already fixed by the scache anyway  -orabidoo
  */
};

struct Server
{
  struct User*     user;        /* who activated this connection */
  const char*      up;          /* Pointer to scache name */
  char             by[NICKLEN + 1];
  struct ConfItem* nline;       /* N-line pointer for this server */
  struct Client*   servers;     /* Servers on this server */
  struct Client*   users;       /* Users on this server */
  int		   tsversion;   /* ts version sent in SVINFO */
};

struct Client
{
  struct Client*    next;
  struct Client*    prev;
  struct Client*    hnext;
  struct Client*    idhnext;

/* QS */

  struct Client*    lnext;      /* Used for Server->servers/users */
  struct Client*    lprev;      /* Used for Server->servers/users */

/* LINKLIST */
  /* N.B. next_local_client, and previous_local_client
   * duplicate the link list referenced to by struct Server -> users
   * someday, we'll rationalize this... -Dianora
   */

  struct Client*    next_local_client;      /* keep track of these */
  struct Client*    previous_local_client;

  struct Client*    next_server_client;
  struct Client*    next_oper_client;

  struct User*      user;       /* ...defined, if this is a User */
  struct Server*    serv;       /* ...defined, if this is a server */
  struct Client*    servptr;    /* Points to server this Client is on */
  struct Client*    from;       /* == self, if Local Client, *NEVER* NULL! */

  struct Whowas*    whowas;     /* Pointers to whowas structs */
  time_t            lasttime;   /* ...should be only LOCAL clients? --msa */
  time_t            firsttime;  /* time client was created */
  time_t            since;      /* last time we parsed something */
  time_t            tsinfo;     /* TS on the nick, SVINFO on server */
  unsigned int      umodes;     /* opers, normal users subset */
  unsigned int      flags;      /* client flags */
  unsigned int      flags2;     /* ugh. overflow */
  int               fd;         /* >= 0, for local clients */
  int               hopcount;   /* number of servers to this 0 = local */
  unsigned short    status;     /* Client type */
  char              nicksent;
  unsigned char     local_flag; /* if this is 1 this client is local */
  short    listprogress;        /* where were we when the /list blocked? */
  int      listprogress2;       /* where in the current bucket were we? */

  /*
   * client->name is the unique name for a client nick or host
   */
  char              name[HOSTLEN + 1]; 
  /* 
   * client->username is the username from ident or the USER message, 
   * If the client is idented the USER message is ignored, otherwise 
   * the username part of the USER message is put here prefixed with a 
   * tilde depending on the I:line, Once a client has registered, this
   * field should be considered read-only.
   */ 
  char              username[USERLEN + 1]; /* client's username */
  /*
   * client->host contains the resolved name or IP address
   * as a string for the user, it may be fiddled with for oper spoofing etc.
   * once it's changed the *real* address goes away. This should be
   * considered a read-only field after the client has registered.
   */
  char              host[HOSTLEN + 1];     /* client's hostname */
  /*
   * client->realhost contains the real resolved name or IP address if
   * client has a spoofed I: Line. This solves the problem above with the
   * *real* host/IP going away after client->host is overwritten. -malign
   */
  char              realhost[HOSTLEN +1];  /* client's real hostname */
  /*
   * client->info for unix clients will normally contain the info from the 
   * gcos field in /etc/passwd but anything can go here.
   */
  char              info[REALLEN + 1]; /* Free form additional client info */
#ifdef FLUD
  struct SLink*     fludees;
#endif
  /*
   * The following fields are allocated only for local clients
   * (directly connected to *this* server with a socket.
   * The first of them *MUST* be the "count"--it is the field
   * to which the allocation is tied to! *Never* refer to
   * these fields, if (from != self).
   */
  int               count;       /* Amount of data in buffer */
#ifdef FLUD
  time_t            fludblock;
  struct fludbot*   fluders;
#endif
#ifdef ANTI_SPAMBOT
  time_t            last_join_time;   /* when this client last 
                                         joined a channel */
  time_t            last_leave_time;  /* when this client last 
                                       * left a channel */
  int               join_leave_count; /* count of JOIN/LEAVE in less than 
                                         MIN_JOIN_LEAVE_TIME seconds */
  int               oper_warn_count_down; /* warn opers of this possible 
                                          spambot every time this gets to 0 */
#endif
#ifdef ANTI_DRONE_FLOOD
  time_t            first_received_message_time;
  int               received_number_of_privmsgs;
  int               drone_noticed;
#endif
  char  buffer[CLIENT_BUFSIZE]; /* Incoming message buffer */
#ifdef ZIP_LINKS
  struct Zdata*     zip;        /* zip data */
#endif
  short             lastsq;     /* # of 2k blocks when sendqueued called last*/
  struct DBuf       sendQ;      /* Outgoing message queue--if socket full */
  struct DBuf       recvQ;      /* Hold for data incoming yet to be parsed */
  /*
   * we want to use unsigned int here so the sizes have a better chance of
   * staying the same on 64 bit machines. The current trend is to use
   * I32LP64, (32 bit ints, 64 bit longs and pointers) and since ircd
   * will NEVER run on an operating system where ints are less than 32 bits, 
   * it's a relatively safe bet to use ints. Since right shift operations are
   * performed on these, it's not safe to allow them to become negative, 
   * which is possible for long running server connections. Unsigned values 
   * generally overflow gracefully. --Bleep
   */
  unsigned int      sendM;      /* Statistics: protocol messages send */
  unsigned int      sendK;      /* Statistics: total k-bytes send */
  unsigned int      receiveM;   /* Statistics: protocol messages received */
  unsigned int      receiveK;   /* Statistics: total k-bytes received */
  unsigned short    sendB;      /* counters to count upto 1-k lots of bytes */
  unsigned short    receiveB;   /* sent and received. */
  unsigned int      lastrecvM;  /* to check for activity --Mika */
  int               priority;
  struct Listener*  listener;   /* listener accepted from */
  struct SLink*     confs;      /* Configuration record associated */
  struct in_addr    ip;         /* keep real ip# too */
  unsigned short    port;       /* and the remote port# too :-) */
  struct DNSQuery*  dns_query;  /* result returned from resolver query */
#ifdef ANTI_NICK_FLOOD
  time_t            last_nick_change;
  int               number_of_nick_changes;
#endif
  time_t            last_knock; /* don't allow knock to flood */
  /*
   * client->sockhost contains the ip address gotten from the socket as a
   * string, this field should be considered read-only once the connection
   * has been made. (set in s_bsd.c only)
   */
  char              sockhost[HOSTIPLEN + 1]; /* This is the host name from the 
                                              socket ip address as string */
  /*
   * XXX - there is no reason to save this, it should be checked when it's
   * received and not stored, this is not used after registration
   */
  char              passwd[PASSWDLEN + 1];
  int               caps;       /* capabilities bit-field */
};

/*
 * status macros.
 */
#define STAT_CONNECTING 0x01   /* -4 */
#define STAT_HANDSHAKE  0x02   /* -3 */
#define STAT_ME         0x04   /* -2 */
#define STAT_UNKNOWN    0x08   /* -1 */
#define STAT_SERVER     0x10   /* 0  */
#define STAT_CLIENT     0x20   /* 1  */


#define IsRegisteredUser(x)     ((x)->status == STAT_CLIENT)
#define IsRegistered(x)         ((x)->status  > STAT_UNKNOWN)
#define IsConnecting(x)         ((x)->status == STAT_CONNECTING)
#define IsHandshake(x)          ((x)->status == STAT_HANDSHAKE)
#define IsMe(x)                 ((x)->status == STAT_ME)
#define IsUnknown(x)            ((x)->status == STAT_UNKNOWN)
#define IsServer(x)             ((x)->status == STAT_SERVER)
#define IsClient(x)             ((x)->status == STAT_CLIENT)

#define SetConnecting(x)        ((x)->status = STAT_CONNECTING)
#define SetHandshake(x)         ((x)->status = STAT_HANDSHAKE)
#define SetMe(x)                ((x)->status = STAT_ME)
#define SetUnknown(x)           ((x)->status = STAT_UNKNOWN)
#define SetServer(x)            ((x)->status = STAT_SERVER)
#define SetClient(x)            ((x)->status = STAT_CLIENT)

#define STAT_CLIENT_PARSE (STAT_UNKNOWN | STAT_CLIENT)
#define STAT_SERVER_PARSE (STAT_CONNECTING | STAT_HANDSHAKE | STAT_SERVER)

#define PARSE_AS_CLIENT(x)      ((x)->status & STAT_CLIENT_PARSE)
#define PARSE_AS_SERVER(x)      ((x)->status & STAT_SERVER_PARSE)

/*
 * ts stuff:
 *  if TS5_ONLY is defined, TS_MIN is 5, else 3
 */
#ifdef TS5
#define TS_CURRENT	5
#else
#define TS_CURRENT	3
#endif

#ifdef TS5_ONLY
#define TS_MIN   	5
#else
#define TS_MIN          3       /* minimum supported TS protocol version */
#endif

#define TS_DOESTS       0x20000000
#define DoesTS(x)       ((x)->tsinfo == TS_DOESTS)


/* housekeeping flags */

#define FLAGS_PINGSENT     0x0001 /* Unreplied ping sent */
#define FLAGS_DEADSOCKET   0x0002 /* Local socket is dead--Exiting soon */
#define FLAGS_KILLED       0x0004 /* Prevents "QUIT" from being sent for this*/
#define FLAGS_BLOCKED      0x0008 /* socket is in a blocked condition */
#define FLAGS_REJECT_HOLD  0x0010 /* client has been klined */
#define FLAGS_CLOSING      0x0020 /* set when closing to suppress errors */
#define FLAGS_CHKACCESS    0x0040 /* ok to check clients access if set */
#define FLAGS_GOTID        0x0080 /* successful ident lookup achieved */
#define FLAGS_NEEDID       0x0100 /* I-lines say must use ident return */
#define FLAGS_NONL         0x0200 /* No \n in buffer */
#define FLAGS_NORMALEX     0x0400 /* Client exited normally */
#define FLAGS_SENDQEX      0x0800 /* Sendq exceeded */
#define FLAGS_IPHASH       0x1000 /* iphashed this client */

/* umodes, settable flags */
#define FLAGS_SERVNOTICE   0x0001 /* server notices such as kill */
#define FLAGS_CCONN        0x0002 /* Client Connections */

#ifdef SERVICES
/* Show nickname as being registered with NickServ -malign */
#define FLAGS_NICKREG      0x0004 /* Client NickServ Registered */
#endif /* SERVICES */

#define FLAGS_SKILL        0x0008 /* Server Killed */
#define FLAGS_FULL         0x0010 /* Full messages */
#define FLAGS_SPY          0x0020 /* see STATS / LINKS */
#define FLAGS_DEBUG        0x0040 /* 'debugging' info */
#define FLAGS_NCHANGE      0x0080 /* Nick change notice */
#define FLAGS_WALLOP       0x0100 /* send wallops to them */
#define FLAGS_OPERWALL     0x0200 /* Operwalls */
#define FLAGS_INVISIBLE    0x0400 /* makes user invisible */
#define FLAGS_BOTS         0x0800 /* shows bots */
#define FLAGS_EXTERNAL     0x1000 /* show servers introduced */
/* user information flags, only settable by remote mode or local oper */
#define FLAGS_ADMIN        0x4000 /* Admin */
#define FLAGS_OPER         0x8000 /* IRC Operator */

/* *sigh* overflow flags */
#define FLAGS2_RESTRICTED   0x0001      /* restricted client */
#define FLAGS2_PING_TIMEOUT 0x0002
#define FLAGS2_E_LINED      0x0004      /* client is graced with E line */
#define FLAGS2_B_LINED      0x0008      /* client is graced with B line */
#define FLAGS2_F_LINED      0x0010      /* client is graced with F line */
#define FLAGS2_EXEMPTGLINE  0x2000      /* client can't be G-lined */

/* oper priv flags */
#define FLAGS2_OPER_GLOBAL_KILL 0x0020  /* oper can global kill */
#define FLAGS2_OPER_REMOTE      0x0040  /* oper can do squits/connects */
#define FLAGS2_OPER_UNKLINE     0x0080  /* oper can use unkline */
#define FLAGS2_OPER_GLINE       0x0100  /* oper can use gline */
#define FLAGS2_OPER_N           0x0200  /* oper can umode n */
#define FLAGS2_OPER_K           0x0400  /* oper can kill/kline */
#define FLAGS2_OPER_DIE         0x0800  /* oper can die */
#define FLAGS2_OPER_REHASH      0x1000  /* oper can rehash */
#define FLAGS2_OPER_FLAGS       (FLAGS2_OPER_GLOBAL_KILL | \
                                 FLAGS2_OPER_REMOTE | \
                                 FLAGS2_OPER_UNKLINE | \
                                 FLAGS2_OPER_GLINE | \
                                 FLAGS2_OPER_N | \
                                 FLAGS2_OPER_K | \
                                 FLAGS2_OPER_DIE | \
                                 FLAGS2_OPER_REHASH)
/* ZIP_LINKS */

#define FLAGS2_ZIP           0x4000  /* (server) link is zipped */
#define FLAGS2_ZIPFIRST      0x8000  /* start of zip (ignore any CR/LF) */
#define FLAGS2_CBURST       0x10000  /* connection burst being sent */

#define FLAGS2_DOINGLIST    0x20000  /* client is doing a list */
#ifdef IDLE_CHECK
#define FLAGS2_IDLE_LINED   0x40000
#endif
#define FLAGS2_ALREADY_EXITED   0x80000         /* kludge grrrr */
#define FLAGS2_IP_SPOOFING      0x100000        /* client IP is spoofed */
#define FLAGS2_IP_HIDDEN        0x200000        /* client IP should be hidden
                                                   from non opers */
#define FLAGS2_SENDQ_POP  0x400000  /* sendq exceeded (during list) */


#define SEND_UMODES  (FLAGS_INVISIBLE | FLAGS_ADMIN | FLAGS_WALLOP)
#define ALL_UMODES   (SEND_UMODES | FLAGS_SERVNOTICE | FLAGS_CCONN | \
                      FLAGS_SKILL | FLAGS_FULL | FLAGS_SPY | \
                      FLAGS_NCHANGE | FLAGS_OPERWALL | FLAGS_DEBUG | \
                      FLAGS_BOTS | FLAGS_EXTERNAL | FLAGS_OPER )

#ifndef ADMIN_UMODES
#define ADMIN_UMODES  (FLAGS_ADMIN | FLAGS_WALLOP | FLAGS_SERVNOTICE | \
                      FLAGS_SPY | FLAGS_OPERWALL | FLAGS_DEBUG | FLAGS_BOTS)
#endif /* OPER_UMODES */

#ifndef OPER_UMODES
#define OPER_UMODES (FLAGS_OPER | FLAGS_WALLOP | FLAGS_SERVNOTICE | \
                      FLAGS_SPY | FLAGS_DEBUG | FLAGS_BOTS)
#endif /* OPER_UMODES */

#define FLAGS_ID     (FLAGS_NEEDID | FLAGS_GOTID)

/*
 * flags macros.
 */
#define IsPerson(x)             (IsClient(x) && (x)->user)
#define DoAccess(x)             ((x)->flags & FLAGS_CHKACCESS)
#define IsLocal(x)              ((x)->flags & FLAGS_LOCAL)
#define IsDead(x)               ((x)->flags & FLAGS_DEADSOCKET)
#define SetAccess(x)            ((x)->flags |= FLAGS_CHKACCESS)
#define NoNewLine(x)            ((x)->flags & FLAGS_NONL)
#define ClearAccess(x)          ((x)->flags &= ~FLAGS_CHKACCESS)
#define MyConnect(x)            ((x)->local_flag != 0)
#define MyClient(x)             (MyConnect(x) && IsClient(x))

/* oper flags */
/* Below was MyOper(x) */
#define MyAdmin(x)              (MyConnect(x) && IsAdmin(x))
/* Below was IsOper(x) */
#define IsAdmin(x)              ((x)->umodes & FLAGS_ADMIN)
/* Below was IsLocop */
#define IsOper(x)               ((x)->umodes & FLAGS_OPER)
/* Below was IsAnOper */
#define IsEmpowered(x)          ((x)->umodes & (FLAGS_ADMIN|FLAGS_OPER))
/* Below was SetOper(x) */
#define SetAdmin(x)             ((x)->umodes |= FLAGS_ADMIN)
/* Below was SetLocOp(x) */
#define SetOper(x)              ((x)->umodes |= FLAGS_OPER)
/* Below was ClearOper(x) */
#define ClearAdmin(x)           ((x)->umodes &= ~FLAGS_ADMIN)
/* Below was ClearLocop(x) */
#define ClearOper(x)            ((x)->umodes &= ~FLAGS_OPER)
#define IsPrivileged(x)         (IsEmpowered(x) || IsServer(x))

#ifdef SERVICES
#define IsNickReg(x)              ((x)->umodes & FLAGS_NICKREG)
#endif /* SERVICES */

/* umode flags */
#define IsInvisible(x)          ((x)->umodes & FLAGS_INVISIBLE)
#define SetInvisible(x)         ((x)->umodes |= FLAGS_INVISIBLE)
#define ClearInvisible(x)       ((x)->umodes &= ~FLAGS_INVISIBLE)
#define SendWallops(x)          (((x)->umodes & FLAGS_WALLOP) && IsEmpowered(x))
#define ClearWallops(x)         ((x)->umodes &= ~FLAGS_WALLOP)
#define SendServNotice(x)       (((x)->umodes & FLAGS_SERVNOTICE) && IsEmpowered(x))
#define SendOperwall(x)         ((x)->umodes & FLAGS_OPERWALL)
#define SendCConnNotice(x)      ((x)->umodes & FLAGS_CCONN)
#define SendSkillNotice(x)      ((x)->umodes & FLAGS_SKILL)
#define SendFullNotice(x)       ((x)->umodes & FLAGS_FULL)
#define SendSpyNotice(x)        ((x)->umodes & FLAGS_SPY)
#define SendDebugNotice(x)      ((x)->umodes & FLAGS_DEBUG)
#define SendNickChange(x)       ((x)->umodes & FLAGS_NCHANGE)
#define SetWallops(x)           ((x)->umodes |= FLAGS_WALLOP)


#ifdef REJECT_HOLD
#define IsRejectHeld(x)         ((x)->flags & FLAGS_REJECT_HOLD)
#define SetRejectHold(x)        ((x)->flags |= FLAGS_REJECT_HOLD)
#endif

#define SetIpHash(x)            ((x)->flags |= FLAGS_IPHASH)
#define ClearIpHash             ((x)->flags &= ~FLAGS_IPHASH)
#define IsIpHash                ((x)->flags & FLAGS_IPHASH)

#define SetNeedId(x)            ((x)->flags |= FLAGS_NEEDID)
#define IsNeedId(x)             (((x)->flags & FLAGS_NEEDID) != 0)

#define SetGotId(x)             ((x)->flags |= FLAGS_GOTID)
#define IsGotId(x)              (((x)->flags & FLAGS_GOTID) != 0)

/*
 * flags2 macros.
 */
#define IsRestricted(x)         ((x)->flags2 & FLAGS2_RESTRICTED)
#define SetRestricted(x)        ((x)->flags2 |= FLAGS2_RESTRICTED)
#define ClearDoingList(x)       ((x)->flags2 &= ~FLAGS2_DOINGLIST)
#define SetDoingList(x)         ((x)->flags2 |= FLAGS2_DOINGLIST)
#define IsDoingList(x)          ((x)->flags2 & FLAGS2_DOINGLIST)
#define ClearSendqPop(x)        ((x)->flags2 &= ~FLAGS2_SENDQ_POP)
#define SetSendqPop(x)          ((x)->flags2 |= FLAGS2_SENDQ_POP)
#define IsSendqPopped(x)        ((x)->flags2 & FLAGS2_SENDQ_POP)
#define IsElined(x)             ((x)->flags2 & FLAGS2_E_LINED)
#define SetElined(x)            ((x)->flags2 |= FLAGS2_E_LINED)
#define IsBlined(x)             ((x)->flags2 & FLAGS2_B_LINED)
#define SetBlined(x)            ((x)->flags2 |= FLAGS2_B_LINED)
#define IsFlined(x)             ((x)->flags2 & FLAGS2_F_LINED)
#define SetFlined(x)            ((x)->flags2 |= FLAGS2_F_LINED)
#define IsExemptGline(x)        ((x)->flags2 & FLAGS2_EXEMPTGLINE)
#define SetExemptGline(x)       ((x)->flags2 |= FLAGS2_EXEMPTGLINE)
#define SetIPSpoof(x)           ((x)->flags2 |= FLAGS2_IP_SPOOFING)
#define IsIPSpoof(x)            ((x)->flags2 & FLAGS2_IP_SPOOFING)
#define SetIPHidden(x)          ((x)->flags2 |= FLAGS2_IP_HIDDEN)
#define IsIPHidden(x)           ((x)->flags2 & FLAGS2_IP_HIDDEN)

#ifdef IDLE_CHECK
#define SetIdlelined(x)         ((x)->flags2 |= FLAGS2_IDLE_LINED)
#define IsIdlelined(x)          ((x)->flags2 & FLAGS2_IDLE_LINED)
#endif

#define SetOperGlobalKill(x)    ((x)->flags2 |= FLAGS2_OPER_GLOBAL_KILL)
#define IsOperGlobalKill(x)     ((x)->flags2 & FLAGS2_OPER_GLOBAL_KILL)
#define SetOperRemote(x)        ((x)->flags2 |= FLAGS2_OPER_REMOTE)
#define IsOperRemote(x)         ((x)->flags2 & FLAGS2_OPER_REMOTE)
#define SetOperUnkline(x)       ((x)->flags2 |= FLAGS2_OPER_UNKLINE)
#define IsSetOperUnkline(x)     ((x)->flags2 & FLAGS2_OPER_UNKLINE)
#define SetOperGline(x)         ((x)->flags2 |= FLAGS2_OPER_GLINE)
#define IsSetOperGline(x)       ((x)->flags2 & FLAGS2_OPER_GLINE)
#define SetOperN(x)             ((x)->flags2 |= FLAGS2_OPER_N)
#define IsSetOperN(x)           ((x)->flags2 & FLAGS2_OPER_N)
#define SetOperK(x)             ((x)->flags2 |= FLAGS2_OPER_K)
#define IsSetOperK(x)           ((x)->flags2 & FLAGS2_OPER_K)
#define SetOperDie(x)           ((x)->flags2 |= FLAGS2_OPER_DIE)
#define IsOperDie(x)            ((x)->flags2 & FLAGS2_OPER_DIE)
#define SetOperRehash(x)        ((x)->flags2 |= FLAGS2_OPER_REHASH)
#define IsOperRehash(x)         ((x)->flags2 & FLAGS2_OPER_REHASH)
#define CBurst(x)               ((x)->flags2 & FLAGS2_CBURST)

/*
 * 'offsetof' is defined in ANSI-C. The following definition
 * is not absolutely portable (I have been told), but so far
 * it has worked on all machines I have needed it. The type
 * should be size_t but...  --msa
 */
#ifndef offsetof
#define offsetof(t, m) (size_t)((&((t *)0L)->m))
#endif

#define CLIENT_LOCAL_SIZE sizeof(struct Client)
#define CLIENT_REMOTE_SIZE offsetof(struct Client, count)

/*
 * definitions for get_client_name
 */
#define HIDE_IP 0
#define SHOW_IP 1
#define MASK_IP 2

extern time_t         check_pings(time_t current);
extern const char*    get_client_name(struct Client* client, int show_ip);
extern const char*    get_client_host(struct Client* client);
extern void           release_client_dns_reply(struct Client* client);
extern void           init_client_heap(void);
extern void           clean_client_heap(void);
extern struct Client* make_client(struct Client* from);
extern void           _free_client(struct Client* client);
extern void           add_client_to_list(struct Client* client);
extern void           remove_client_from_list(struct Client *);
extern void           add_client_to_llist(struct Client** list, 
                                          struct Client* client);
extern void           del_client_from_llist(struct Client** list, 
                                            struct Client* client);
extern int            exit_client(struct Client*, struct Client*, 
                                  struct Client*, const char* comment);

extern void     count_local_client_memory(int *, int *);
extern void     count_remote_client_memory(int *, int *);
extern  int     check_registered (struct Client *);
extern  int     check_registered_user (struct Client *);

extern struct Client* find_chasing (struct Client *, char *, int *);
extern struct Client* find_client(const char* name, struct Client* client);
extern struct Client* find_server_by_name(const char* name);
extern struct Client* find_person (char *, struct Client *);
extern struct Client* find_server(const char* name);
extern struct Client* find_userhost (char *, char *, struct Client *, int *);
extern struct Client* next_client(struct Client* next, const char* name);
extern struct Client* next_client_double(struct Client* next, 
                                         const char* name);

/* 
 * Time we allow clients to spend in unknown state, before tossing.
 */
#define UNKNOWN_TIME 20

#endif /* INCLUDED_client_h */
