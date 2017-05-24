/************************************************************************
 *   IRC - Internet Relay Chat, src/s_user.c
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
 *  $Id: s_user.c,v 1.3 2007/02/14 10:00:44 malign Exp $
 */

#include "m_commands.h"
#include "s_user.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "flud.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "motd.h"
#include "msg.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"
#include "flud.h"

#ifdef ANTI_DRONE_FLOOD
#include "dbuf.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>


static int do_user (char *, aClient *, aClient*, char *, char *, char *,
                     char *);

static int nickkilldone(aClient *, aClient *, int, char **, time_t, char *);
static int valid_hostname(const char* hostname);
static int valid_username(const char* username);
static void report_and_set_user_flags( aClient *, aConfItem * );
static int tell_user_off(aClient *,char **);

/* table of ascii char letters to corresponding bitmask */

static FLAG_ITEM user_modes[] =
{ 
  {FLAGS_ADMIN, 'a'},
  {FLAGS_BOTS,  'b'},
  {FLAGS_CCONN, 'c'},
  {FLAGS_DEBUG, 'd'},
  {FLAGS_FULL,  'f'},
  {FLAGS_INVISIBLE, 'i'},
  {FLAGS_SKILL, 'k'},
  {FLAGS_NCHANGE, 'n'},
  {FLAGS_OPER, 'o'},
#ifdef SERVICES
  {FLAGS_NICKREG, 'r'},
#endif /* SERVICES */
  {FLAGS_SERVNOTICE, 's'},
  {FLAGS_WALLOP, 'w'},
  {FLAGS_EXTERNAL, 'x'},
  {FLAGS_SPY, 'y'},
  {FLAGS_OPERWALL, 'z'},
  {0, 0}
};

/* memory is cheap. map 0-255 to equivalent mode */

static int user_modes_from_c_to_bitmask[] =
{ 
  /* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x0F */
  /* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x1F */
  /* 0x20 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x2F */
  /* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x3F */
  0,            /* @ */
  0,            /* A */
  0,            /* B */
  0,            /* C */
  0,            /* D */
  0,            /* E */
  0,            /* F */
  0,            /* G */
  0,            /* H */
  0,            /* I */
  0,            /* J */
  0,            /* K */
  0,            /* L */
  0,            /* M */
  0,            /* N */
  0,            /* O */
  0,            /* P */
  0,            /* Q */
  0,            /* R */
  0,            /* S */
  0,            /* T */
  0,            /* U */
  0,            /* V */
  0,            /* W */
  0,            /* X */
  0,            /* Y */
  0,            /* Z 0x5A */
  0, 0, 0, 0, 0, /* 0x5F */ 
  /* 0x60 */       0,
  FLAGS_ADMIN,  /* a */
  FLAGS_BOTS,   /* b */
  FLAGS_CCONN,  /* c */
  FLAGS_DEBUG,  /* d */
  0,            /* e */
  FLAGS_FULL,   /* f */
  0,            /* g */
  0,            /* h */
  FLAGS_INVISIBLE, /* i */
  0,            /* j */
  FLAGS_SKILL,  /* k */
  0,            /* l */
  0,            /* m */
  FLAGS_NCHANGE, /* n */
  FLAGS_OPER,   /* o */
  0,            /* p */
  0,            /* q */
#ifdef SERVICES
  FLAGS_NICKREG,    /* r */
#endif /* SERVICES */
  FLAGS_SERVNOTICE, /* s */
  0,            /* t */
  0,            /* u */
  0,            /* v */
  FLAGS_WALLOP, /* w */
  FLAGS_EXTERNAL, /* x */
  FLAGS_SPY,    /* y */
  FLAGS_OPERWALL, /* z 0x7A */
  0,0,0,0,0,     /* 0x7B - 0x7F */

  /* 0x80 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x9F */
  /* 0x90 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x9F */
  /* 0xA0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xAF */
  /* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xBF */
  /* 0xC0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xCF */
  /* 0xD0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xDF */
  /* 0xE0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xEF */
  /* 0xF0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  /* 0xFF */
};


unsigned long my_rand(void);    /* provided by orabidoo */



/*
 * show_opers - send the client a list of opers
 * inputs       - pointer to client to show opers to
 * output       - none
 * side effects - show who is opered on this server
 */
void show_opers(struct Client *cptr)
{
  struct Client        *cptr2;
  int j=0;

  for(cptr2 = oper_cptr_list; cptr2; cptr2 = cptr2->next_oper_client)
    {
      ++j;
      if (MyClient(cptr) && IsEmpowered(cptr))
        {
          sendto_one(cptr, ":%s %d %s :[%c][%s] %s (%s@%s) Idle: %d",
                     me.name, RPL_STATSDEBUG, cptr->name,
                     IsAdmin(cptr2) ? 'O' : 'a',
                     oper_privs_as_string(cptr2,
                                          cptr2->confs->value.aconf->port),
                     cptr2->name,
                     cptr2->username, cptr2->host,
                     CurrentTime - cptr2->user->last);
        }
      else
        {
          sendto_one(cptr, ":%s %d %s :[%c] %s (%s@%s) Idle: %d",
                     me.name, RPL_STATSDEBUG, cptr->name,
                     IsAdmin(cptr2) ? 'O' : 'a',
                     cptr2->name,
                     cptr2->username, cptr2->host,
                     CurrentTime - cptr2->user->last);
        }
    }

  sendto_one(cptr, ":%s %d %s :%d OPER%s", me.name, RPL_STATSDEBUG,
             cptr->name, j, (j==1) ? "" : "s");
}

/*
 * show_lusers - total up counts and display to client
 */
int show_lusers(struct Client *cptr, struct Client *sptr, 
                int parc, char *parv[])
{
#define LUSERS_CACHE_TIME 180
  static long last_time=0;
  static int    s_count = 0, c_count = 0, u_count = 0, i_count = 0;
  static int    o_count = 0, m_client = 0, m_servers = 0;
  int forced;
  struct Client *acptr;

/*  forced = (parc >= 2); */
  forced = (IsEmpowered(sptr) && (parc > 3));

/* (void)collapse(parv[1]); */

  Count.unknown = 0;
  m_servers = Count.myserver;
  m_client  = Count.local;
  i_count   = Count.invisi;
  u_count   = Count.unknown;
  c_count   = Count.total-Count.invisi;
  s_count   = Count.server;
  o_count   = Count.oper;
  if (forced || (CurrentTime > last_time+LUSERS_CACHE_TIME))
    {
      last_time = CurrentTime;
      /* only recount if more than a second has passed since last request */
      /* use LUSERS_CACHE_TIME instead... */
      s_count = 0; c_count = 0; u_count = 0; i_count = 0;
      o_count = 0; m_client = 0; m_servers = 0;

      for (acptr = GlobalClientList; acptr; acptr = acptr->next)
        {
          switch (acptr->status)
            {
            case STAT_SERVER:
              if (MyConnect(acptr))
                m_servers++;
            case STAT_ME:
              s_count++;
              break;
            case STAT_CLIENT:
              if (IsEmpowered(acptr))
                o_count++;
#ifdef  SHOW_INVISIBLE_LUSERS
              if (MyConnect(acptr))
                m_client++;
              if (!IsInvisible(acptr))
                c_count++;
              else
                i_count++;
#else
              if (MyConnect(acptr))
                {
                  if (IsInvisible(acptr))
                    {
                      if (IsEmpowered(sptr))
                        m_client++;
                    }
                  else
                    m_client++;
                }
              if (!IsInvisible(acptr))
                c_count++;
              else
                i_count++;
#endif
              break;
            default:
              u_count++;
              break;
            }
        }
      /*
       * We only want to reassign the global counts if the recount
       * time has expired, and NOT when it was forced, since someone
       * may supply a mask which will only count part of the userbase
       *        -Taner
       */
      if (!forced)
        {
          if (m_servers != Count.myserver)
            {
              sendto_realops_flags(FLAGS_DEBUG, 
                                 "Local server count off by %d",
                                 Count.myserver - m_servers);
              Count.myserver = m_servers;
            }
          if (s_count != Count.server)
            {
              sendto_realops_flags(FLAGS_DEBUG,
                                 "Server count off by %d",
                                 Count.server - s_count);
              Count.server = s_count;
            }
          if (i_count != Count.invisi)
            {
              sendto_realops_flags(FLAGS_DEBUG,
                                 "Invisible client count off by %d",
                                 Count.invisi - i_count);
              Count.invisi = i_count;
            }
          if ((c_count+i_count) != Count.total)
            {
              sendto_realops_flags(FLAGS_DEBUG, "Total client count off by %d",
                                 Count.total - (c_count+i_count));
              Count.total = c_count+i_count;
            }
          if (m_client != Count.local)
            {
              sendto_realops_flags(FLAGS_DEBUG,
                                 "Local client count off by %d",
                                 Count.local - m_client);
              Count.local = m_client;
            }
          if (o_count != Count.oper)
            {
              sendto_realops_flags(FLAGS_DEBUG,
                                 "Oper count off by %d", Count.oper - o_count);
              Count.oper = o_count;
            }
          Count.unknown = u_count;
        } /* Complain & reset loop */
    } /* Recount loop */
  
#ifndef SHOW_INVISIBLE_LUSERS
  if (IsEmpowered(sptr) && i_count)
#endif
    sendto_one(sptr, form_str(RPL_LUSERCLIENT), me.name, parv[0],
               c_count, i_count, s_count);
#ifndef SHOW_INVISIBLE_LUSERS
  else
    sendto_one(sptr,
               ":%s %d %s :There are %d users on %d servers", me.name,
               RPL_LUSERCLIENT, parv[0], c_count,
               s_count);
#endif
  if (o_count)
    sendto_one(sptr, form_str(RPL_LUSEROP),
               me.name, parv[0], o_count);
  if (u_count > 0)
    sendto_one(sptr, form_str(RPL_LUSERUNKNOWN),
               me.name, parv[0], u_count);
  /* This should be ok */
  if (Count.chan > 0)
    sendto_one(sptr, form_str(RPL_LUSERCHANNELS),
               me.name, parv[0], Count.chan);
  sendto_one(sptr, form_str(RPL_LUSERME),
             me.name, parv[0], m_client, m_servers);
  sendto_one(sptr, form_str(RPL_LOCALUSERS), me.name, parv[0],
             Count.local, Count.max_loc);
  sendto_one(sptr, form_str(RPL_GLOBALUSERS), me.name, parv[0],
             Count.total, Count.max_tot);

  sendto_one(sptr, form_str(RPL_STATSCONN), me.name, parv[0],
             MaxConnectionCount, MaxClientCount,
             Count.totalrestartcount);
  if (m_client > MaxClientCount)
    MaxClientCount = m_client;
  if ((m_client + m_servers) > MaxConnectionCount)
    {
      MaxConnectionCount = m_client + m_servers;
    }

  return 0;
}

  

/*
** m_functions execute protocol messages on this server:
**
**      cptr    is always NON-NULL, pointing to a *LOCAL* client
**              structure (with an open socket connected!). This
**              identifies the physical socket where the message
**              originated (or which caused the m_function to be
**              executed--some m_functions may call others...).
**
**      sptr    is the source of the message, defined by the
**              prefix part of the message if present. If not
**              or prefix not found, then sptr==cptr.
**
**              (!IsServer(cptr)) => (cptr == sptr), because
**              prefixes are taken *only* from servers...
**
**              (IsServer(cptr))
**                      (sptr == cptr) => the message didn't
**                      have the prefix.
**
**                      (sptr != cptr && IsServer(sptr) means
**                      the prefix specified servername. (?)
**
**                      (sptr != cptr && !IsServer(sptr) means
**                      that message originated from a remote
**                      user (not local).
**
**              combining
**
**              (!IsServer(sptr)) means that, sptr can safely
**              taken as defining the target structure of the
**              message in this server.
**
**      *Always* true (if 'parse' and others are working correct):
**
**      1)      sptr->from == cptr  (note: cptr->from == cptr)
**
**      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
**              *cannot* be a local connection, unless it's
**              actually cptr!). [MyConnect(x) should probably
**              be defined as (x == x->from) --msa ]
**
**      parc    number of variable parameter strings (if zero,
**              parv is allowed to be NULL)
**
**      parv    a NULL terminated list of parameter pointers,
**
**                      parv[0], sender (prefix string), if not present
**                              this points to an empty string.
**                      parv[1]...parv[parc-1]
**                              pointers to additional parameters
**                      parv[parc] == NULL, *always*
**
**              note:   it is guaranteed that parv[0]..parv[parc-1] are all
**                      non-NULL pointers.
*/

/*
 * clean_nick_name - ensures that the given parameter (nick) is
 * really a proper string for a nickname (note, the 'nick'
 * may be modified in the process...)
 *
 *      RETURNS the length of the final NICKNAME (0, if
 *      nickname is illegal)
 *
 *  Nickname characters are in range
 *      'A'..'}', '_', '-', '0'..'9'
 *  anything outside the above set will terminate nickname.
 *  In addition, the first character cannot be '-'
 *  or a Digit.
 *
 *  Note:
 *      '~'-character should be allowed, but
 *      a change should be global, some confusion would
 *      result if only few servers allowed it...
 */
int clean_nick_name(char* nick)
{
  char* ch   = nick;
  char* endp = ch + NICKLEN;
  assert(0 != nick);

  if (*nick == '-' || IsDigit(*nick)) /* first character in [0..9-] */
    return 0;
  
  for ( ; ch < endp && *ch; ++ch) {
    if (!IsNickChar(*ch))
      break;
  }
  *ch = '\0';

  return (ch - nick);
}

/*
** register_user
**      This function is called when both NICK and USER messages
**      have been accepted for the client, in whatever order. Only
**      after this, is the USER message propagated.
**
**      NICK's must be propagated at once when received, although
**      it would be better to delay them too until full info is
**      available. Doing it is not so simple though, would have
**      to implement the following:
**
**      (actually it has been implemented already for a while) -orabidoo
**
**      1) user telnets in and gives only "NICK foobar" and waits
**      2) another user far away logs in normally with the nick
**         "foobar" (quite legal, as this server didn't propagate
**         it).
**      3) now this server gets nick "foobar" from outside, but
**         has already the same defined locally. Current server
**         would just issue "KILL foobar" to clean out dups. But,
**         this is not fair. It should actually request another
**         nick from local user or kill him/her...
*/

static int register_user(aClient *cptr, aClient *sptr, 
                         char *nick, char *username)
{
  aConfItem*  aconf;
  char*       parv[3];
  static char ubuf[12];
  anUser*     user = sptr->user;
  char*       reason;
  char        tmpstr2[512];

  assert(0 != sptr);
  assert(sptr->username != username);

  user->last = CurrentTime;
  parv[0] = sptr->name;
  parv[1] = parv[2] = NULL;

  /* pointed out by Mortiis, never be too careful */
  if(strlen(username) > USERLEN)
    username[USERLEN] = '\0';

  reason = NULL;

#define NOT_AUTHORIZED  (-1)
#define SOCKET_ERROR    (-2)
#define I_LINE_FULL     (-3)
#define I_LINE_FULL2    (-4)
#define BANNED_CLIENT   (-5)

  if (MyConnect(sptr))
    {
      switch( check_client(sptr,username,&reason))
        {
        case SOCKET_ERROR:
          return exit_client(cptr, sptr, &me, "Socket Error");
          break;

        case I_LINE_FULL:
        case I_LINE_FULL2:
          sendto_realops_flags(FLAGS_FULL, "%s for %s (%s).",
                               "I-line is full",
                               get_client_host(sptr),
                               inetntoa((char*) &sptr->ip));
          Log("Too many connections from %s.", get_client_host(sptr));
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me, 
                 "No more connections allowed in your connection class" );
          break;

        case NOT_AUTHORIZED:

#ifdef REJECT_HOLD

          /* Slow down the reconnectors who are rejected */
          if( (reject_held_fds != REJECT_HELD_MAX ) )
            {
              SetRejectHold(cptr);
              reject_held_fds++;
              release_client_dns_reply(cptr);
              return 0;
            }
          else
#endif
            {
              ServerStats->is_ref++;
	/* jdc - lists server name & port connections are on */
	/*       a purely cosmetical change */
              sendto_realops_flags(FLAGS_CCONN,
				 "%s from %s [%s] on [%s/%u].",
                                 "Unauthorized client connection",
                                 get_client_host(sptr),
                                 inetntoa((char *)&sptr->ip),
				 sptr->listener->name,
				 sptr->listener->port
				 );
              Log("Unauthorized client connection from %s on [%s/%u].",
                  get_client_host(sptr),
		  sptr->listener->name,
		  sptr->listener->port
		  );

              ServerStats->is_ref++;
              return exit_client(cptr, sptr, &me,
                                 "You are not authorized to use this server");
            }
          break;

        case BANNED_CLIENT:
          {
            if (!IsGotId(sptr))
              {
                if (IsNeedId(sptr))
                  {
                    *sptr->username = '~';
                    strncpy_irc(&sptr->username[1], username, USERLEN - 1);
                  }
                else
                  strncpy_irc(sptr->username, username, USERLEN);
                sptr->username[USERLEN] = '\0';
              }

            if ( tell_user_off( sptr, &reason ))
              {
                ServerStats->is_ref++;
                return exit_client(cptr, sptr, &me, "Banned" );
              }
            else
              return 0;

            break;
          }
        default:
          release_client_dns_reply(cptr);
          break;
        }

      if(!valid_hostname(sptr->host))
        {
          sendto_one(sptr,":%s NOTICE %s :*** Notice -- You have an illegal character in your hostname", 
                     me.name, sptr->name );

          strncpy(sptr->host,sptr->sockhost,HOSTIPLEN+1);
        }

      aconf = sptr->confs->value.aconf;
      if (!aconf)
        return exit_client(cptr, sptr, &me, "*** Not Authorized");
      if (!IsGotId(sptr))
        {
          if (IsNeedIdentd(aconf))
            {
              ServerStats->is_ref++;
              sendto_one(sptr,
 ":%s NOTICE %s :*** Notice -- You need to install identd to use this server",
                         me.name, cptr->name);
               return exit_client(cptr, sptr, &me, "Install identd");
             }
           if (IsNoTilde(aconf))
             {
                strncpy_irc(sptr->username, username, USERLEN);
             }
           else
             {
                *sptr->username = '~';
                strncpy_irc(&sptr->username[1], username, USERLEN - 1);
             }
           sptr->username[USERLEN] = '\0';
        }

      /* password check */
      if (!BadPtr(aconf->passwd) && 0 != strcmp(sptr->passwd, aconf->passwd))
        {
          ServerStats->is_ref++;
          sendto_one(sptr, form_str(ERR_PASSWDMISMATCH),
                     me.name, parv[0]);
          return exit_client(cptr, sptr, &me, "Bad Password");
        }
      memset(sptr->passwd,0, sizeof(sptr->passwd));

      /* report if user has &^>= etc. and set flags as needed in sptr */
      report_and_set_user_flags(sptr, aconf);

      /* Limit clients */
      /*
       * We want to be able to have servers and F-line clients
       * connect, so save room for "buffer" connections.
       * Smaller servers may want to decrease this, and it should
       * probably be just a percentage of the MAXCLIENTS...
       *   -Taner
       */
      /* Except "F:" clients */
      if ( (
          ((Count.local + 1) >= (MAXCLIENTS+MAX_BUFFER))) ||
            (((Count.local +1) >= (MAXCLIENTS - 5)) && !(IsFlined(sptr))))
        {
          sendto_realops_flags(FLAGS_FULL,
                               "Too many clients, rejecting %s[%s].",
                               nick, sptr->host);
          ServerStats->is_ref++;
          return exit_client(cptr, sptr, &me,
                             "Sorry, server is full - try later");
        }

      /* valid user name check */

      if (!valid_username(sptr->username))
        {
          sendto_realops_flags(FLAGS_CCONN,"Invalid username: %s (%s@%s)",
                             nick, sptr->username, sptr->host);
          ServerStats->is_ref++;
          ircsprintf(tmpstr2, "Invalid username [%s]", sptr->username);
          return exit_client(cptr, sptr, &me, tmpstr2);
        }
      /* end of valid user name check */

      if(!IsEmpowered(sptr))
        {
          char *xreason;

          if ( (aconf = find_special_conf(sptr->info,CONF_XLINE)))
            {
              if(aconf->passwd)
                xreason = aconf->passwd;
              else
                xreason = "NONE";
              
              if(aconf->port)
                {
                  if (aconf->port == 1)
                    {
                      sendto_realops_flags(FLAGS_CCONN,
                                           "X-line Rejecting [%s] [%s], user %s",
                                           sptr->info,
                                           xreason,
                                           get_client_name(cptr, FALSE));
                    }
                  ServerStats->is_ref++;      
                  return exit_client(cptr, sptr, &me, "Bad user info");
                }
              else
                sendto_realops_flags(FLAGS_CCONN,
                                   "X-line Warning [%s] [%s], user %s",
                                   sptr->info,
                                   xreason,
                                   get_client_name(cptr, FALSE));
            }
         }

#ifndef NO_DEFAULT_INVISIBLE
      sptr->umodes |= FLAGS_INVISIBLE;
#endif

      sendto_realops_flags(FLAGS_CCONN,
                         "Client connecting: %s (%s@%s) [%s] {%d}",
                         nick, sptr->username, sptr->host,

#ifdef HIDE_SPOOF_IPS
                         IsIPSpoof(sptr) ? "255.255.255.255" : 
#endif /* HIDE_SPOOF_IPS */
                         inetntoa((char *)&sptr->ip),
                         get_client_class(sptr));

      if(IsInvisible(sptr))
        Count.invisi++;

      if ((++Count.local) > Count.max_loc)
        {
          Count.max_loc = Count.local;
          if (!(Count.max_loc % 10))
            sendto_ops("New Max Local Clients: %d",
                       Count.max_loc);
        }
    }
  else
    strncpy_irc(sptr->username, username, USERLEN);

  SetClient(sptr);

  sptr->servptr = find_server(user->server);
  if (!sptr->servptr)
    {
      sendto_realops("Ghost killed: %s on invalid server %s",
                 sptr->name, sptr->user->server);
      sendto_one(cptr,":%s KILL %s :%s (Ghosted, %s doesn't exist)",
                 me.name, sptr->name, me.name, user->server);
      sptr->flags |= FLAGS_KILLED;
      return exit_client(NULL, sptr, &me, "Ghost");
    }
  add_client_to_llist(&(sptr->servptr->serv->users), sptr);

/* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;

  if (MyConnect(sptr))
    {
      sendto_one(sptr, form_str(RPL_WELCOME), me.name, nick, nick);
      /* This is a duplicate of the NOTICE but see below...*/
      sendto_one(sptr, form_str(RPL_YOURHOST), me.name, nick,
                 get_listener_name(sptr->listener), ircd_version);
      
      /*
      ** Don't mess with this one - IRCII needs it! -Avalon
      */
      sendto_one(sptr,
                 "NOTICE %s :*** Your host is %s, running %s.",
                 nick, get_listener_name(sptr->listener), ircd_version);
      
      sendto_one(sptr, form_str(RPL_CREATED),me.name,nick,creation);
      sendto_one(sptr, form_str(RPL_MYINFO), me.name, parv[0],
                 me.name, ircd_version);
      sendto_one(sptr, form_str(RPL_ISUPPORT), me.name, parv[0], isupport);
      /* Increment the total number of clients since (re)start */
      Count.totalrestartcount++;
      show_lusers(sptr, sptr, 1, parv);

#ifdef SHORT_MOTD
      sendto_one(sptr,"NOTICE %s :*** Notice -- motd was last changed at %s",
                 sptr->name,
                 ConfigFileEntry.motd.lastChangedDate);

      sendto_one(sptr,
                 "NOTICE %s :*** Notice -- Please read the motd if you haven't read it",
                 sptr->name);
      
      sendto_one(sptr, form_str(RPL_MOTDSTART),
                 me.name, sptr->name, me.name);
      
      sendto_one(sptr,
                 form_str(RPL_MOTD),
                 me.name, sptr->name,
                 "*** This is the short motd ***"
                 );

      sendto_one(sptr, form_str(RPL_ENDOFMOTD),
                 me.name, sptr->name);
#else
      SendMessageFile(sptr, &ConfigFileEntry.motd);
#endif
      
#ifdef LITTLE_I_LINES
      if(sptr->confs && sptr->confs->value.aconf &&
         (sptr->confs->value.aconf->flags
          & CONF_FLAGS_LITTLE_I_LINE))
        {
          SetRestricted(sptr);
          sendto_one(sptr,"NOTICE %s :*** Notice -- You are in a restricted access mode",nick);
          sendto_one(sptr,"NOTICE %s :*** Notice -- You can not chanop others",nick);
        }
#endif

#ifdef NEED_SPLITCODE
      if (server_was_split)
        {
          sendto_one(sptr,"NOTICE %s :*** Notice -- %s is currently experiencing a netsplit.", nick, me.name);
        }

      nextping = CurrentTime;
#endif

    }
  else if (IsServer(cptr))
    {
      aClient *acptr;
      if ((acptr = find_server(user->server)) && acptr->from != sptr->from)
        {
          sendto_realops_flags(FLAGS_DEBUG, 
                             "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
                             cptr->name, nick, sptr->username,
                             sptr->host, user->server,
                             acptr->name, acptr->from->name);
          sendto_one(cptr,
                     ":%s KILL %s :%s (%s != %s[%s] USER from wrong direction)",
                     me.name, sptr->name, me.name, user->server,
                     acptr->from->name, acptr->from->host);
          sptr->flags |= FLAGS_KILLED;
          return exit_client(sptr, sptr, &me,
                             "USER server wrong direction");
          
        }
      /*
       * Super GhostDetect:
       *        If we can't find the server the user is supposed to be on,
       * then simply blow the user away.        -Taner
       */
      if (!acptr)
        {
          sendto_one(cptr,
                     ":%s KILL %s :%s GHOST (no server %s on the net)",
                     me.name,
                     sptr->name, me.name, user->server);
          sendto_realops("No server %s for user %s[%s@%s] from %s",
                          user->server, sptr->name, sptr->username,
                          sptr->host, sptr->from->name);
          sptr->flags |= FLAGS_KILLED;
          return exit_client(sptr, sptr, &me, "Ghosted Client");
        }
    }

  if(MyClient(sptr))
    send_umode(sptr, sptr, 0, SEND_UMODES, ubuf);
  else
    send_umode(NULL, sptr, 0, SEND_UMODES, ubuf);

  if (!*ubuf)
    {
      ubuf[0] = '+';
      ubuf[1] = '\0';
    }
  
  /* LINKLIST 
   * add to local client link list -Dianora
   * I really want to move this add to link list
   * inside the if (MyConnect(sptr)) up above
   * but I also want to make sure its really good and registered
   * local client
   *
   * double link list only for clients, traversing
   * a small link list for opers/servers isn't a big deal
   * but it is for clients -Dianora
   */

  if (MyConnect(sptr))
    {
      if(local_cptr_list)
        local_cptr_list->previous_local_client = sptr;
      sptr->previous_local_client = (aClient *)NULL;
      sptr->next_local_client = local_cptr_list;
      local_cptr_list = sptr;
    }
  
  sendto_serv_butone(cptr, "NICK %s %d %lu %s %s %s %s :%s",
                     nick, sptr->hopcount+1, sptr->tsinfo, ubuf,
                     sptr->username, sptr->host, user->server,
                     sptr->info);
  return 0;
}

/* 
 * valid_hostname - check hostname for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot and
 * will not allow more dots than chars.
 */
static int valid_hostname(const char* hostname)
{
  int         dots  = 0;
  int         chars = 0;
  const char* p     = hostname;

  assert(0 != p);

  if ('.' == *p)
    return NO;

  while (*p) {
    if (!IsHostChar(*p))
      return NO;
    if ('.' == *p++)
      ++dots;
    else
      ++chars;
  }
  return (0 == dots || chars < dots) ? NO : YES;
}

/* 
 * valid_username - check username for validity
 *
 * Inputs       - pointer to user
 * Output       - YES if valid, NO if not
 * Side effects - NONE
 * 
 * Absolutely always reject any '*' '!' '?' '@' '.' in an user name
 * reject any odd control characters names.
 */
static int valid_username(const char* username)
{
  const char *p = username;
  assert(0 != p);

  if ('~' == *p)
    ++p;
  /* 
   * reject usernames that don't start with an alphanum
   * i.e. reject jokers who have '-@somehost' or '.@somehost'
   * or "-hi-@somehost", "h-----@somehost" would still be accepted.
   *
   * -Dianora
   */
  if (!IsAlNum(*p))
    return NO;

  while (*++p) {
    if (!IsUserChar(*p))
      return NO;
  }
  return YES;
}

/* 
 * tell_user_off
 *
 * inputs       - client pointer of user to tell off
 *              - pointer to reason user is getting told off
 * output       - drop connection now YES or NO (for reject hold)
 * side effects -
 */

static int
tell_user_off(aClient *cptr, char **preason )
{
#ifdef KLINE_WITH_REASON
  char* p = 0;
#endif /* KLINE_WITH_REASON */

  /* Ok... if using REJECT_HOLD, I'm not going to dump
   * the client immediately, but just mark the client for exit
   * at some future time, .. this marking also disables reads/
   * writes from the client. i.e. the client is "hanging" onto
   * an fd without actually being able to do anything with it
   * I still send the usual messages about the k line, but its
   * not exited immediately.
   * - Dianora
   */
            
#ifdef REJECT_HOLD
  if( (reject_held_fds != REJECT_HELD_MAX ) )
    {
      SetRejectHold(cptr);
      reject_held_fds++;
#endif

#ifdef KLINE_WITH_REASON
      if(*preason)
        {
          if(( p = strchr(*preason, '|')) )
            *p = '\0';

          sendto_one(cptr, ":%s NOTICE %s :*** Banned: %s",
                     me.name,cptr->name,*preason);
            
          if(p)
            *p = '|';
        }
      else
#endif
        sendto_one(cptr, ":%s NOTICE %s :*** Banned: No Reason",
                   me.name,cptr->name);
#ifdef REJECT_HOLD
      return NO;
    }
#endif

  return YES;
}

/* report_and_set_user_flags
 *
 * Inputs       - pointer to sptr
 *              - pointer to aconf for this user
 * Output       - NONE
 * Side effects -
 * Report to user any special flags they are getting, and set them.
 */

static void 
report_and_set_user_flags(aClient *sptr,aConfItem *aconf)
{
  /* If this user is being spoofed, tell them so */
  if(IsConfDoSpoofIp(aconf))
    {
      sendto_one(sptr,
                 ":%s NOTICE %s :*** Spoofing your hostname/IP.",
                 me.name,sptr->name);
    }

  /* If this user is in the exception class, Set it "E lined" */
  if(IsConfElined(aconf))
    {
      SetElined(sptr);
      sendto_one(sptr,
         ":%s NOTICE %s :*** You are exempt from K/D/G lines.",
                 me.name,sptr->name);
    }
#ifdef GLINES
  else if(IsConfExemptGline(aconf))
    {
      SetExemptGline(sptr);
      sendto_one(sptr,
         ":%s NOTICE %s :*** You are exempt from G lines.",
                 me.name,sptr->name);
    }
#endif /* GLINES */

  /* If this user is exempt from user limits set it F lined" */
  if(IsConfFlined(aconf))
    {
      SetFlined(sptr);
      sendto_one(sptr,
                 ":%s NOTICE %s :*** You are exempt from user limits.",
                 me.name,sptr->name);
    }
#ifdef IDLE_CHECK
  /* If this user is exempt from idle time outs */
  if(IsConfIdlelined(aconf))
    {
      SetIdlelined(sptr);
      sendto_one(sptr,
         ":%s NOTICE %s :*** You are exempt from idle limits.",
                 me.name,sptr->name);
    }
#endif
}

/*
 * nickkilldone
 *
 * input        - pointer to physical aClient
 *              - pointer to source aClient
 *              - argument count
 *              - arguments
 *              - newts time
 *              - nick
 * output       -
 * side effects -
 */

static int nickkilldone(aClient *cptr, aClient *sptr, int parc,
                 char *parv[], time_t newts,char *nick)
{

  if (IsServer(sptr))
    {
      /* A server introducing a new client, change source */
      
      sptr = make_client(cptr);
      add_client_to_list(sptr);         /* double linked list */
      if (parc > 2)
        sptr->hopcount = atoi(parv[2]);
      if (newts)
        sptr->tsinfo = newts;
      else
        {
          newts = sptr->tsinfo = CurrentTime;
          ts_warn("Remote nick %s (%s) introduced without a TS", nick, parv[0]);
        }
      /* copy the nick in place */
      (void)strcpy(sptr->name, nick);
      (void)add_to_client_hash_table(nick, sptr);
      if (parc > 8)
        {
          int   flag;
          char* m;
          
          /*
          ** parse the usermodes -orabidoo
          */
          m = &parv[4][1];
          while (*m)
            {
              flag = user_modes_from_c_to_bitmask[(unsigned char)*m];
              if( flag & FLAGS_INVISIBLE )
                {
                  Count.invisi++;
                }
              if( flag & FLAGS_ADMIN )
                {
                  Count.oper++;
                }
              sptr->umodes |= flag & SEND_UMODES;
              m++;
            }
          
          return do_user(nick, cptr, sptr, parv[5], parv[6],
                         parv[7], parv[8]);
        }
    }
  else if (sptr->name[0])
    {
      /*
      ** Client just changing his/her nick. If he/she is
      ** on a channel, send note of change to all clients
      ** on that channel. Propagate notice to other servers.
      */
      if (irccmp(parv[0], nick))
        sptr->tsinfo = newts ? newts : CurrentTime;

      if(MyConnect(sptr) && IsRegisteredUser(sptr))
        {     
#ifdef ANTI_NICK_FLOOD

          if( (sptr->last_nick_change + MAX_NICK_TIME) < CurrentTime)
            sptr->number_of_nick_changes = 0;
          sptr->last_nick_change = CurrentTime;
            sptr->number_of_nick_changes++;

          if(sptr->number_of_nick_changes <= MAX_NICK_CHANGES)
            {
#endif
              sendto_realops_flags(FLAGS_NCHANGE,
                                 "Nick change: From %s to %s [%s@%s]",
                                 parv[0], nick, sptr->username,
                                 sptr->host);

              sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
              if (sptr->user)
                {
                  add_history(sptr,1);
              
                  sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                                     parv[0], nick, sptr->tsinfo);
                }
#ifdef ANTI_NICK_FLOOD
            }
          else
            {
              sendto_one(sptr,
                         form_str(ERR_NICKTOOFAST),
			 me.name, sptr->name, sptr->name, nick, MAX_NICK_TIME);
              return 0;
            }
#endif
        }
      else
        {
          sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
          if (sptr->user)
            {
              add_history(sptr,1);
              sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                                 parv[0], nick, sptr->tsinfo);
            }
        }
    }
  else
    {
      /* Client setting NICK the first time */
      

      /* This had to be copied here to avoid problems.. */
      strcpy(sptr->name, nick);
      sptr->tsinfo = CurrentTime;
      if (sptr->user)
        {
          char buf[USERLEN + 1];
          strncpy_irc(buf, sptr->username, USERLEN);
          buf[USERLEN] = '\0';
          /*
          ** USER already received, now we have NICK.
          ** *NOTE* For servers "NICK" *must* precede the
          ** user message (giving USER before NICK is possible
          ** only for local client connection!). register_user
          ** may reject the client and call exit_client for it
          ** --must test this and exit m_nick too!!!
          */
            if (register_user(cptr, sptr, nick, buf) == CLIENT_EXITED)
              return CLIENT_EXITED;
        }
    }

  /*
  **  Finally set new nick name.
  */
  if (sptr->name[0])
    del_from_client_hash_table(sptr->name, sptr);
  strcpy(sptr->name, nick);
  add_to_client_hash_table(nick, sptr);

  return 0;
}

/*
** m_nick
**      parv[0] = sender prefix
**      parv[1] = nickname
**      parv[2] = optional hopcount when new user; TS when nick change
**      parv[3] = optional TS
**      parv[4] = optional umode
**      parv[5] = optional username
**      parv[6] = optional hostname
**      parv[7] = optional server
**      parv[8] = optional ircname
*/
int m_nick(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  aClient  *acptr = NULL;
  char     nick[NICKLEN + 2];
  char     *s = NULL;
  time_t   newts = 0;
  int      sameuser = 0;
  int      fromTS = 0;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }

  if (!IsServer(sptr) && IsServer(cptr) && parc > 2)
    newts = atol(parv[2]);
  else if (IsServer(sptr) && parc > 3)
    newts = atol(parv[3]);
  else parc = 2;

  /*
   * parc == 2 on a normal client sign on (local) and a normal
   *      client nick change
   * parc == 4 on a normal server-to-server client nick change
   *      notice
   * parc == 9 on a normal TS style server-to-server NICK
   *      introduction
   */
  if ((IsServer(sptr)) && (parc < 9))
    {
      /*
       * We got the wrong number of params. Someone is trying
       * to trick us. Kill it. -ThemBones
       * As discussed with ThemBones, not much point to this code now
       * sending a whack of global kills would also be more annoying
       * then its worth, just note the problem, and continue
       * -Dianora
       */
      ts_warn("BAD NICK: %s[%s@%s] on %s (from %s)", parv[1],
                     (parc >= 6) ? parv[5] : "-",
                     (parc >= 7) ? parv[6] : "-",
                     (parc >= 8) ? parv[7] : "-", parv[0]);
      return 0;
    }

  if ((parc >= 7) && (!strchr(parv[6], '.')) && (!strchr(parv[6], ':')))
    {
      /*
       * Ok, we got the right number of params, but there
       * isn't a single dot in the hostname, which is suspicious.
       * Don't fret about it just kill it. - ThemBones
       */
      /* IPv6 allows hostnames without .'s, so check if it
       * has ":"'s also. --fl_
       */
      ts_warn("BAD HOSTNAME: %s[%s@%s] on %s (from %s)",
                     parv[0], parv[5], parv[6], parv[7], parv[0]);
      return 0;
    }

  fromTS = (parc > 6);

  /*
   * XXX - ok, we terminate the nick with the first '~' ?  hmmm..
   *  later we allow them? isvalid used to think '~'s were ok
   *  IsNickChar allows them as well
   */
  if (MyConnect(sptr) && (s = strchr(parv[1], '~')))
    *s = '\0';
  /*
   * nick is an auto, need to terminate the string
   */
  strncpy_irc(nick, parv[1], NICKLEN);
  nick[NICKLEN] = '\0';

  /*
   * if clean_nick_name() returns a null name OR if the server sent a nick
   * name and clean_nick_name() changed it in some way (due to rules of nick
   * creation) then reject it. If from a server and we reject it,
   * and KILL it. -avalon 4/4/92
   */
  if (clean_nick_name(nick) == 0 ||
      (IsServer(cptr) && strcmp(nick, parv[1])))
    {
      sendto_one(sptr, form_str(ERR_ERRONEUSNICKNAME),
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], parv[1]);
      
      if (IsServer(cptr))
        {
          ServerStats->is_kill++;
          sendto_realops_flags(FLAGS_DEBUG, "Bad Nick: %s From: %s %s",
                             parv[1], parv[0],
#ifndef HIDE_SERVERS_IPS
                             get_client_name(cptr, FALSE));
#else
                             get_client_name(cptr, MASK_IP));
#endif
          sendto_one(cptr, ":%s KILL %s :%s (%s <- %s[%s])",
                     me.name, parv[1], me.name, parv[1],
                     nick, cptr->name);
          if (sptr != cptr) /* bad nick change */
            {
              sendto_serv_butone(cptr,
                                 ":%s KILL %s :%s (%s <- %s!%s@%s)",
                                 me.name, parv[0], me.name,
#ifndef HIDE_SERVERS_IPS
                                 get_client_name(cptr, FALSE),
#else
                                 cptr->name,
#endif
                                 parv[0],
                                 sptr->username,
                                 sptr->user ? sptr->user->server :
                                 cptr->name);
              sptr->flags |= FLAGS_KILLED;
              return exit_client(cptr,sptr,&me,"BadNick");
            }
        }
      return 0;
    }

  if(MyConnect(sptr) && !IsServer(sptr) && !IsEmpowered(sptr) &&
     find_q_line(nick, sptr->username, sptr->host)) 
    {
      sendto_realops_flags(FLAGS_CCONN,
                         "Quarantined nick [%s] from user %s",
                         nick,get_client_name(cptr, FALSE));
      sendto_one(sptr, form_str(ERR_ERRONEUSNICKNAME),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  /*
  ** Check against nick name collisions.
  **
  ** Put this 'if' here so that the nesting goes nicely on the screen :)
  ** We check against server name list before determining if the nickname
  ** is present in the nicklist (due to the way the below for loop is
  ** constructed). -avalon
  */
  if ((acptr = find_server(nick)))
    if (MyConnect(sptr))
      {
        sendto_one(sptr, form_str(ERR_NICKNAMEINUSE), me.name,
                   BadPtr(parv[0]) ? "*" : parv[0], nick);
        return 0; /* NICK message ignored */
      }
  /*
  ** acptr already has result from previous find_server()
  */
  /*
   * Well. unless we have a capricious server on the net,
   * a nick can never be the same as a server name - Dianora
   */

  if (acptr)
    {
      /*
      ** We have a nickname trying to use the same name as
      ** a server. Send out a nick collision KILL to remove
      ** the nickname. As long as only a KILL is sent out,
      ** there is no danger of the server being disconnected.
      ** Ultimate way to jupiter a nick ? >;-). -avalon
      */
      sendto_realops("Nick collision on %s(%s <- %s)",
                 sptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                 get_client_name(cptr, FALSE));
#else
                 cptr->name);
#endif
      ServerStats->is_kill++;
      sendto_one(cptr, ":%s KILL %s :%s (%s <- %s)",
                 me.name, sptr->name, me.name, acptr->from->name,
                 /* NOTE: Cannot use get_client_name
                 ** twice here, it returns static
                 ** string pointer--the other info
                 ** would be lost
                 */
#ifndef HIDE_SERVERS_IPS
                 get_client_name(cptr, FALSE));
#else
                 cptr->name);
#endif
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick/Server collision");
    }
  

  if (!(acptr = find_client(nick, NULL)))
    return(nickkilldone(cptr,sptr,parc,parv,newts,nick));  /* No collisions,
                                                       * all clear...
                                                       */

  /*
  ** If acptr == sptr, then we have a client doing a nick
  ** change between *equivalent* nicknames as far as server
  ** is concerned (user is changing the case of his/her
  ** nickname or somesuch)
  */
  if (acptr == sptr)
   {
    if (strcmp(acptr->name, nick) != 0)
      /*
      ** Allows change of case in his/her nick
      */
      return(nickkilldone(cptr,sptr,parc,parv,newts,nick)); /* -- go and process change */
    else
      {
        /*
        ** This is just ':old NICK old' type thing.
        ** Just forget the whole thing here. There is
        ** no point forwarding it to anywhere,
        ** especially since servers prior to this
        ** version would treat it as nick collision.
        */
        return 0; /* NICK Message ignored */
      }
   }
  /*
  ** Note: From this point forward it can be assumed that
  ** acptr != sptr (point to different client structures).
  */


  /*
  ** If the older one is "non-person", the new entry is just
  ** allowed to overwrite it. Just silently drop non-person,
  ** and proceed with the nick. This should take care of the
  ** "dormant nick" way of generating collisions...
  */
  if (IsUnknown(acptr)) 
   {
    if (MyConnect(acptr))
      {
        exit_client(NULL, acptr, &me, "Overridden");
        return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
      }
    else
      {
        if (fromTS && !(acptr->user))
          {
            sendto_realops("Nick Collision on %s(%s(NOUSER) <- %s!%s@%s)(TS:%s)",
                   acptr->name, acptr->from->name, parv[1], parv[5], parv[6],
                   cptr->name);

#ifndef LOCAL_NICK_COLLIDE
	    sendto_serv_butone(NULL, /* all servers */
		       ":%s KILL %s :%s (%s(NOUSER) <- %s!%s@%s)(TS:%s)",
			       me.name,
			       acptr->name,
			       me.name,
			       acptr->from->name,
			       parv[1],
			       parv[5],
			       parv[6],
			       cptr->name);
#endif

            acptr->flags |= FLAGS_KILLED;
            /* Having no USER struct should be ok... */
            return exit_client(cptr, acptr, &me,
                           "Got TS NICK before Non-TS USER");
        }
      }
   }
  /*
  ** Decide, we really have a nick collision and deal with it
  */
  if (!IsServer(cptr))
    {
      /*
      ** NICK is coming from local client connection. Just
      ** send error reply and ignore the command.
      */
      sendto_one(sptr, form_str(ERR_NICKNAMEINUSE),
                 /* parv[0] is empty when connecting */
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], nick);
      return 0; /* NICK message ignored */
    }
  /*
  ** NICK was coming from a server connection. Means that the same
  ** nick is registered for different users by different server.
  ** This is either a race condition (two users coming online about
  ** same time, or net reconnecting) or just two net fragments becoming
  ** joined and having same nicks in use. We cannot have TWO users with
  ** same nick--purge this NICK from the system with a KILL... >;)
  */
  /*
  ** This seemingly obscure test (sptr == cptr) differentiates
  ** between "NICK new" (TRUE) and ":old NICK new" (FALSE) forms.
  */
  /* 
  ** Changed to something reasonable like IsServer(sptr)
  ** (true if "NICK new", false if ":old NICK new") -orabidoo
  */

  if (IsServer(sptr))
    {
      /* As discussed with chris (comstud) nick kills can
       * be handled locally, provided all NICK's are propogated
       * globally. Just like channel joins are handled.
       *
       * I think I got this right. 
       * -Dianora
       * There are problems with this, due to lag it looks like.
       * backed out for now...
       */
#ifdef LOCAL_NICK_COLLIDE
      /* just propogate it through */
      sendto_serv_butone(cptr, ":%s NICK %s :%lu",
                         parv[0], nick, sptr->tsinfo);
#endif
      /*
      ** A new NICK being introduced by a neighbouring
      ** server (e.g. message type "NICK new" received)
      */
      if (!newts || !acptr->tsinfo
          || (newts == acptr->tsinfo))
        {
          sendto_realops("Nick collision on %s(%s <- %s)(both killed)",
                     acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS	
                     get_client_name(cptr, FALSE));
#else
                     cptr->name);
#endif
          ServerStats->is_kill++;
          sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                     me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(NULL, /* all servers */
			     ":%s KILL %s :%s (%s <- %s)",
			     me.name, acptr->name, me.name,
			     acptr->from->name,
			     /* NOTE: Cannot use get_client_name twice
			     ** here, it returns static string pointer:
			     ** the other info would be lost
			     */
#ifndef HIDE_SERVERS_IPS
			     get_client_name(cptr, FALSE));
#else
                             cptr->name);
#endif
#endif
          acptr->flags |= FLAGS_KILLED;
          return exit_client(cptr, acptr, &me, "Nick collision");
        }
      else
        {
          sameuser =  fromTS && (acptr->user) &&
            irccmp(acptr->username, parv[5]) == 0 &&
            irccmp(acptr->host, parv[6]) == 0;
          if ((sameuser && newts < acptr->tsinfo) ||
              (!sameuser && newts > acptr->tsinfo))
            return 0;
          else
            {
              if (sameuser)
                sendto_realops("Nick collision on %s(%s <- %s)(older killed)",
                           acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                           get_client_name(cptr, FALSE));
#else
                           cptr->name);
#endif
              else
                sendto_realops("Nick collision on %s(%s <- %s)(newer killed)",
                           acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                           get_client_name(cptr, FALSE));
#else
                           cptr->name);
#endif
              
              ServerStats->is_kill++;
              sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                         me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	      sendto_serv_butone(sptr, /* all servers but sptr */
				 ":%s KILL %s :%s (%s <- %s)",
				 me.name, acptr->name, me.name,
				 acptr->from->name,
#ifndef HIDE_SERVERS_IPS
				 get_client_name(cptr, FALSE));
#else
                                 cptr->name);
#endif
#endif

              acptr->flags |= FLAGS_KILLED;
              (void)exit_client(cptr, acptr, &me, "Nick collision");
              return nickkilldone(cptr,sptr,parc,parv,newts,nick);
            }
        }
    }
  /*
  ** A NICK change has collided (e.g. message type
  ** ":old NICK new". This requires more complex cleanout.
  ** Both clients must be purged from this server, the "new"
  ** must be killed from the incoming connection, and "old" must
  ** be purged from all outgoing connections.
  */
  if ( !newts || !acptr->tsinfo || (newts == acptr->tsinfo) ||
      !sptr->user)
    {
      sendto_realops("Nick change collision from %s to %s(%s <- %s)(both killed)",
                 sptr->name, acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                 get_client_name(cptr, FALSE));
#else
                 cptr->name);
#endif
      ServerStats->is_kill++;
      sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                 me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
      sendto_serv_butone(NULL, /* KILL old from outgoing servers */
			 ":%s KILL %s :%s (%s(%s) <- %s)",
			 me.name, sptr->name, me.name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
			 acptr->name, get_client_name(cptr, FALSE));
#else
			 acptr->name, cptr->name);
#endif
#endif

      ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
      sendto_serv_butone(NULL, /* Kill new from incoming link */
			 ":%s KILL %s :%s (%s <- %s(%s))",
			 me.name, acptr->name, me.name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
			 get_client_name(cptr, FALSE), sptr->name);
#else
			 cptr->name, sptr->name);
#endif
#endif

      acptr->flags |= FLAGS_KILLED;
      (void)exit_client(NULL, acptr, &me, "Nick collision(new)");
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick collision(old)");
    }
  else
    {
      sameuser = irccmp(acptr->username, sptr->username) == 0 &&
                 irccmp(acptr->host, sptr->host) == 0;
      if ((sameuser && newts < acptr->tsinfo) ||
          (!sameuser && newts > acptr->tsinfo))
        {
          if (sameuser)
            sendto_realops("Nick change collision from %s to %s(%s <- %s)(older killed)",
                       sptr->name, acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                       get_client_name(cptr, FALSE));
#else
                       cptr->name);
#endif
          else
            sendto_realops("Nick change collision from %s to %s(%s <- %s)(newer killed)",
                       sptr->name, acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                       get_client_name(cptr, FALSE));
#else
                       cptr->name);
#endif
          ServerStats->is_kill++;

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(cptr, /* KILL old from outgoing servers */
			     ":%s KILL %s :%s (%s(%s) <- %s)",
			     me.name, sptr->name, me.name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
			     acptr->name, get_client_name(cptr, FALSE));
#else
                             acptr->name, cptr->name);
#endif
#endif

          sptr->flags |= FLAGS_KILLED;
          if (sameuser)
            return exit_client(cptr, sptr, &me, "Nick collision(old)");
          else
            return exit_client(cptr, sptr, &me, "Nick collision(new)");
        }
      else
        {
          if (sameuser)
            sendto_realops("Nick collision on %s(%s <- %s)(older killed)",
                       acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                       get_client_name(cptr, FALSE));
#else
                       cptr->name);
#endif
          else
            sendto_realops("Nick collision on %s(%s <- %s)(newer killed)",
                       acptr->name, acptr->from->name,
#ifndef HIDE_SERVERS_IPS
                       get_client_name(cptr, FALSE));
#else
                       cptr->name);
#endif
          
          ServerStats->is_kill++;
          sendto_one(acptr, form_str(ERR_NICKCOLLISION),
                     me.name, acptr->name, acptr->name);

#ifndef LOCAL_NICK_COLLIDE
	  sendto_serv_butone(sptr, /* all servers but sptr */
			     ":%s KILL %s :%s (%s <- %s)",
			     me.name, acptr->name, me.name,
			     acptr->from->name,
#ifndef HIDE_SERVERS_IPS
			     get_client_name(cptr, FALSE));
#else
			     cptr->name);
#endif
#endif

          acptr->flags |= FLAGS_KILLED;
          (void)exit_client(cptr, acptr, &me, "Nick collision");
          /* goto nickkilldone; */
        }
    }
  return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
}

/* Code provided by orabidoo */
/* a random number generator loosely based on RC5;
   assumes ints are at least 32 bit */
 
unsigned long my_rand() {
  static unsigned long s = 0, t = 0, k = 12345678;
  int i;
 
  if (s == 0 && t == 0) {
    s = (unsigned long)getpid();
    t = (unsigned long)time(NULL);
  }
  for (i=0; i<12; i++) {
    s = (((s^t) << (t&31)) | ((s^t) >> (31 - (t&31)))) + k;
    k += s + t;
    t = (((t^s) << (s&31)) | ((t^s) >> (31 - (s&31)))) + k;
    k += s + t;
  }
  return s;
}


/*
** m_user
**      parv[0] = sender prefix
**      parv[1] = username (login name, account)
**      parv[2] = client host name (used only from other servers)
**      parv[3] = server host name (used only from other servers)
**      parv[4] = users real name info
*/
int m_user(aClient* cptr, aClient* sptr, int parc, char *parv[])
{
#define UFLAGS  (FLAGS_INVISIBLE|FLAGS_WALLOP|FLAGS_SERVNOTICE)
  char* username;
  char* host;
  char* server;
  char* realname;
 
  if (parc > 2 && (username = strchr(parv[1],'@')))
    *username = '\0'; 
  if (parc < 5 || *parv[1] == '\0' || *parv[2] == '\0' ||
      *parv[3] == '\0' || *parv[4] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, BadPtr(parv[0]) ? "*" : parv[0], "USER");
      if (IsServer(cptr))
        sendto_realops("bad USER param count for %s from %s",
#ifndef HIDE_SERVERS_IPS
                       parv[0], get_client_name(cptr, FALSE));
#else
                       parv[0], get_client_name(cptr, MASK_IP));
#endif
      else
        return 0;
    }

  /* Copy parameters into better documenting variables */

  username = (parc < 2 || BadPtr(parv[1])) ? "<bad-boy>" : parv[1];
  host     = (parc < 3 || BadPtr(parv[2])) ? "<nohost>" : parv[2];
  server   = (parc < 4 || BadPtr(parv[3])) ? "<noserver>" : parv[3];
  realname = (parc < 5 || BadPtr(parv[4])) ? "<bad-realname>" : parv[4];
  

  return do_user(parv[0], cptr, sptr, username, host, server, realname);
}


/*
** do_user
*/
static int do_user(char* nick, aClient* cptr, aClient* sptr,
                   char* username, char *host, char *server, char *realname)
{
  struct User* user;

  assert(0 != sptr);
  assert(sptr->username != username);

  user = make_user(sptr);

  if (!MyConnect(sptr))
    {
      /*
       * coming from another server, take the servers word for it
       */
      user->server = find_or_add(server);
      strncpy_irc(sptr->host, host, HOSTLEN); 
    }
  else
    {
      if (!IsUnknown(sptr))
        {
          sendto_one(sptr, form_str(ERR_ALREADYREGISTRED), me.name, nick);
          return 0;
        }

#if 0
      /* Undocumented lame extension to the protocol
       * if user provides a number, the bits set are
       * used to set the umode flags. arghhhhh -db
       */
      sptr->flags |= (UFLAGS & atoi(host));
#endif

      /*
       * don't take the clients word for it, ever
       *  strncpy_irc(user->host, host, HOSTLEN); 
       */
      user->server = me.name;
    }
  strncpy_irc(sptr->info, realname, REALLEN);

  if (sptr->name[0]) /* NICK already received, now I have USER... */
    return register_user(cptr, sptr, sptr->name, username);
  else
    {
      if (!IsGotId(sptr)) 
        {
          /*
           * save the username in the client
           */
          strncpy_irc(sptr->username, username, USERLEN);
        }
    }
  return 0;
}

/*
 * user_mode - set get current users mode
 *
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int user_mode(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  int   flag;
  int   i;
  char  **p, *m;
  aClient *acptr;
  int   what, setflags;
  int   badflag = NO;   /* Only send one bad flag notice -Dianora */
  char  buf[BUFSIZE];

  what = MODE_ADD;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "MODE");
      return 0;
    }

  if (!(acptr = find_person(parv[1], NULL)))
    {
      if (MyConnect(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[1]);
      return 0;
    }

  /*if (IsServer(sptr) || sptr != acptr || acptr->from != sptr->from)*/
  if ((sptr != acptr && !IsServer(sptr)) || (acptr->from != sptr->from && !IsServer(sptr)))
    {
    /* if (IsServer(cptr))
        sendto_ops_butone(NULL, &me,
                          ":%s WALLOPS :MODE for User %s From %s!%s",
                          me.name, parv[1],
#ifndef HIDE_SERVERS_IPS
                          get_client_name(cptr, FALSE), sptr->name);
#else
                          cptr->name, sptr->name);
#endif */
                  /* while hiding the servers ips, lets send a server-sent
                     wallop about it! good idea! -gnp */

      /* else */
        sendto_one(sptr, form_str(ERR_USERSDONTMATCH),
                   me.name, parv[0]);
     return 0;
    }

  if (parc < 3)
    {
      m = buf;
      *m++ = '+';

      for (i = 0; user_modes[i].letter && (m - buf < BUFSIZE - 4);i++)
        if (sptr->umodes & user_modes[i].mode)
          *m++ = user_modes[i].letter;
      *m = '\0';
      sendto_one(sptr, form_str(RPL_UMODEIS), me.name, parv[0], buf);
      return 0;
    }

  /* find flags already set for user */
  setflags = sptr->umodes;
  
  /*
   * parse mode change string(s)
   */
  for (p = &parv[2]; p && *p; p++ )
    for (m = *p; *m; m++)
      switch(*m)
        {
        case '+' :
          what = MODE_ADD;
          break;
        case '-' :
          what = MODE_DEL;
          break;        

        case 'O': case 'a' :
          if(what == MODE_ADD)
            {
              if(IsServer(cptr) && !IsAdmin(sptr))
                {
                  ++Count.oper;
                  SetAdmin(sptr);
                }
            }
          else
            {
	      /* Only decrement the oper counts if an oper to begin with
               * found by Pat Szuta, Perly , perly@xnet.com 
               */

              if(!IsEmpowered(sptr))
                break;

              sptr->umodes &= ~(FLAGS_ADMIN|FLAGS_OPER);

              Count.oper--;

              if (MyConnect(sptr))
                {
                  aClient *prev_cptr = (aClient *)NULL;
                  aClient *cur_cptr = oper_cptr_list;

                  fdlist_delete(sptr->fd, FDL_OPER | FDL_BUSY);
                  detach_conf(sptr,sptr->confs->value.aconf);
                  sptr->flags2 &= ~(FLAGS2_OPER_GLOBAL_KILL|
                                    FLAGS2_OPER_REMOTE|
                                    FLAGS2_OPER_UNKLINE|
                                    FLAGS2_OPER_GLINE|
                                    FLAGS2_OPER_N|
                                    FLAGS2_OPER_K);
                  while(cur_cptr)
                    {
                      if(sptr == cur_cptr) 
                        {
                          if(prev_cptr)
                            prev_cptr->next_oper_client = cur_cptr->next_oper_client;
                          else
                            oper_cptr_list = cur_cptr->next_oper_client;
                          cur_cptr->next_oper_client = (aClient *)NULL;
                          break;
                        }
                      else
                        prev_cptr = cur_cptr;
                      cur_cptr = cur_cptr->next_oper_client;
                    }
                }
            }
          break;

          /* we may not get these,
           * but they shouldnt be in default
           */
        case ' ' :
        case '\n' :
        case '\r' :
        case '\t' :
          break;
        default :
          if( (flag = user_modes_from_c_to_bitmask[(unsigned char)*m]))
            {
              if (what == MODE_ADD)
               {
                if (IsServer(sptr)) /* Allow servers to change usermode flags -malign */
                  acptr->umodes |=flag;
                else
                  sptr->umodes |= flag;
               }
              else
               {
                if (IsServer(sptr))
                  acptr->umodes &=~flag;
                else
                  sptr->umodes &= ~flag;
               }  
            }
          else
            {
              if ( MyConnect(sptr))
                badflag = YES;
            }
          break;
        }

  if(badflag)
    sendto_one(sptr, form_str(ERR_UMODEUNKNOWNFLAG), me.name, parv[0]);

  if (IsServer(sptr)) /* Allow servers to change usermodes -malign */
    {
     sendto_one(acptr, ":%s MODE %s :%s",sptr->name, acptr->name, parv[2]);
/*        sendto_ops_butone(NULL, &me,
                          ":%s WALLOPS :MODE for user %s from %s!%s",
                          me.name, parv[1],
#ifndef HIDE_SERVERS_IPS
                          get_client_name(cptr, FALSE), sptr->name);
#else
                          cptr->name, sptr->name);
#endif */
    }

  if ((sptr->umodes & FLAGS_NCHANGE) && !IsSetOperN(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :*** You need oper and N flag for +n",
                 me.name,parv[0]);
      sptr->umodes &= ~FLAGS_NCHANGE; /* only tcm's really need this */
    }

  if (!(setflags & FLAGS_INVISIBLE) && IsInvisible(sptr))
    ++Count.invisi;
  if ((setflags & FLAGS_INVISIBLE) && !IsInvisible(sptr))
    --Count.invisi;
  /*
   * compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */
  send_umode_out(cptr, sptr, setflags);

  return 0;
}
        
/*
 * send the MODE string for user (user) to connection cptr
 * -avalon
 */
void send_umode(aClient *cptr, aClient *sptr, int old, int sendmask,
                char *umode_buf)
{
  int   i;
  int flag;
  char  *m;
  int   what = MODE_NULL;

  /*
   * build a string in umode_buf to represent the change in the user's
   * mode between the new (sptr->flag) and 'old'.
   */
  m = umode_buf;
  *m = '\0';

  for (i = 0; user_modes[i].letter; i++ )
    {
      flag = user_modes[i].mode;

      if (MyClient(sptr) && !(flag & sendmask))
        continue;
      if ((flag & old) && !(sptr->umodes & flag))
        {
          if (what == MODE_DEL)
            *m++ = user_modes[i].letter;
          else
            {
              what = MODE_DEL;
              *m++ = '-';
              *m++ = user_modes[i].letter;
            }
        }
      else if (!(flag & old) && (sptr->umodes & flag))
        {
          if (what == MODE_ADD)
            *m++ = user_modes[i].letter;
          else
            {
              what = MODE_ADD;
              *m++ = '+';
              *m++ = user_modes[i].letter;
            }
        }
    }
  *m = '\0';
  if (*umode_buf && cptr)
    sendto_one(cptr, ":%s MODE %s :%s", sptr->name, sptr->name, umode_buf);
}

/*
 * added Sat Jul 25 07:30:42 EST 1992
 */
/*
 * extra argument evenTS added to send to TS servers or not -orabidoo
 *
 * extra argument evenTS no longer needed with TS only th+hybrid
 * server -Dianora
 */
void send_umode_out(aClient *cptr,
                       aClient *sptr,
                       int old)
{
  aClient *acptr;
  char buf[BUFSIZE];

  send_umode(NULL, sptr, old, SEND_UMODES, buf);

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      if((acptr != cptr) && (acptr != sptr) && (*buf))
        {
          sendto_one(acptr, ":%s MODE %s :%s",
                   sptr->name, sptr->name, buf);
        }
    }

  if (cptr && MyClient(cptr))
    send_umode(cptr, sptr, old, ALL_UMODES, buf);
}
