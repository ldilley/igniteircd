/************************************************************************
 *   IRC - Internet Relay Chat, src/m_set.c
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
 *   $Id: m_set.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "send.h"
#include "common.h"   /* for NO */
#include "channel.h"  /* for server_was_split */
/*#include "s_log.h"*/

#include <stdlib.h>  /* atoi */

/*
 * m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

/*
 * set_parser - find the correct int. to return 
 * so we can switch() it.
 * KEY:  0 - MAX
 *       1 - AUTOCONN
 *       2 - IDLETIME
 *       3 - FLUDNUM
 *       4 - FLUDTIME
 *       5 - FLUDBLOCK
 *       6 - DRONETIME
 *       7 - DRONECOUNT
 *       8 - SPLITDELAY
 *       9 - SPLITNUM
 *      10 - SPLITUSERS
 *      11 - SPAMNUM
 *      12 - SPAMTIME
 *      13 - LOG
 * - rjp
 *
 * Currently, the end of the table is TOKEN_BAD, 14.  If you add anything
 * to the set table, you must increase TOKEN_BAD so that it is directly
 * after the last valid entry.
 * -Hwy
 */

#define TOKEN_MAX 0
#define TOKEN_AUTOCONN 1
#define TOKEN_IDLETIME 2
#define TOKEN_FLUDNUM 3
#define TOKEN_FLUDTIME 4
#define TOKEN_FLUDBLOCK 5
#define TOKEN_DRONETIME 6
#define TOKEN_DRONECOUNT 7
#define TOKEN_SPLITDELAY 8
#define TOKEN_SPLITNUM 9
#define TOKEN_SPLITUSERS 10
#define TOKEN_SPAMNUM 11
#define TOKEN_SPAMTIME 12
#define TOKEN_LOG 13
#define TOKEN_BAD 14

static char *set_token_table[] = {
  "MAX",
  "AUTOCONN",
  "IDLETIME",
  "FLUDNUM",
  "FLUDTIME",
  "FLUDBLOCK",
  "DRONETIME",
  "DRONECOUNT",
  "SPLITDELAY",
  "SPLITNUM",
  "SPLITUSERS",
  "SPAMNUM",
  "SPAMTIME",
  "LOG",
  NULL
};

static int set_parser(const char* parsethis)
{
  int i;

  for( i = 0; set_token_table[i]; i++ )
    {
      if(!irccmp(set_token_table[i], parsethis))
        return i;
    }
  return TOKEN_BAD;
}

/*
 * m_set - SET command handler
 * set options while running
 */
int m_set(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *command;
  int cnum;

  if (!MyClient(sptr) || !IsEmpowered(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      command = parv[1];
      cnum = set_parser(command);
/* This strcasecmp crap is annoying.. a switch() would be better.. 
 * - rjp
 */
      switch(cnum)
        {
        case TOKEN_MAX:
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value > MASTER_MAX)
                {
                  sendto_one(sptr,
                             ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
                             me.name, parv[0], MASTER_MAX);
                  return 0;
                }
              if (new_value < 32)
                {
                  sendto_one(sptr, ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
                             me.name, parv[0], MAXCLIENTS, highest_fd);
                  return 0;
                }
              MAXCLIENTS = new_value;
              sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
                             parv[0], sptr->username, sptr->host, MAXCLIENTS, Count.local);
              return 0;
            }
          sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
                     me.name, parv[0], MAXCLIENTS, Count.local);
          return 0;
          break;

        case TOKEN_AUTOCONN:
          if(parc > 3)
            {
              int newval = atoi(parv[3]);

              if(!irccmp(parv[2],"ALL"))
                {
                  sendto_realops(
                                 "%s has changed AUTOCONN ALL to %i",
                                 parv[0], newval);
                  GlobalSetOptions.autoconn = newval;
                }
              else
                set_autoconn(sptr,parv[0],parv[2],newval);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
                         me.name, parv[0], GlobalSetOptions.autoconn);
            }
          return 0;
          break;

#ifdef IDLE_CHECK
          case TOKEN_IDLETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled IDLE_CHECK",
                                   parv[0]);
                    IDLETIME = 0;
                  }
                else
                  {
                    sendto_realops("%s has changed IDLETIME to %i",
                                   parv[0], newval);
                    IDLETIME = (newval*60);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
                           me.name, parv[0], IDLETIME/60);
              }
            return 0;
            break;
#endif
#ifdef FLUD
          case TOKEN_FLUDNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDNUM = newval;
                sendto_realops("%s has changed FLUDNUM to %i",
                               parv[0], FLUDNUM);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDNUM is currently %i",
                           me.name, parv[0], FLUDNUM);
              }
            return 0;
            break;

          case TOKEN_FLUDTIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDTIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDTIME = newval;
                sendto_realops("%s has changed FLUDTIME to %i",
                               parv[0], FLUDTIME);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDTIME is currently %i",
                           me.name, parv[0], FLUDTIME);
              }
            return 0;       
            break;

          case TOKEN_FLUDBLOCK:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK must be >= 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDBLOCK = newval;
                if(FLUDBLOCK == 0)
                  {
                    sendto_realops("%s has disabled flud detection/protection",
                                   parv[0]);
                  }
                else
                  {
                    sendto_realops("%s has changed FLUDBLOCK to %i",
                                   parv[0],FLUDBLOCK);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK is currently %i",
                           me.name, parv[0], FLUDBLOCK);
              }
            return 0;       
            break;
#endif
#ifdef ANTI_DRONE_FLOOD
          case TOKEN_DRONETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :DRONETIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                DRONETIME = newval;
                if(DRONETIME == 0)
                  sendto_realops("%s has disabled the ANTI_DRONE_FLOOD code",
                                 parv[0]);
                else
                  sendto_realops("%s has changed DRONETIME to %i",
                                 parv[0], DRONETIME);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :DRONETIME is currently %i",
                           me.name, parv[0], DRONETIME);
              }
            return 0;
            break;

        case TOKEN_DRONECOUNT:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT must be > 0",
                             me.name, parv[0]);
                  return 0;
                }       
              DRONECOUNT = newval;
              sendto_realops("%s has changed DRONECOUNT to %i",
                             parv[0], DRONECOUNT);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT is currently %i",
                         me.name, parv[0], DRONECOUNT);
            }
          return 0;
          break;
#endif
#ifdef NEED_SPLITCODE

            case TOKEN_SPLITDELAY:
              if(parc > 2)
                {
                  int newval = atoi(parv[2]);
                  
                  if(newval < 0)
                    {
                      sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY must be > 0",
                                 me.name, parv[0]);
                      return 0;
                    }
                  /* sygma found it, the hard way */
                  if(newval > MAX_SERVER_SPLIT_RECOVERY_TIME)
                    {
                      sendto_one(sptr,
                                 ":%s NOTICE %s :Cannot set SPLITDELAY over %d",
                                 me.name, parv[0], MAX_SERVER_SPLIT_RECOVERY_TIME);
                      newval = MAX_SERVER_SPLIT_RECOVERY_TIME;
                    }
                  sendto_realops("%s has changed SPLITDELAY to %i",
                                 parv[0], newval);
                  SPLITDELAY = (newval*60);
                  if(SPLITDELAY == 0)
                    {
                      cold_start = NO;
                      if (server_was_split)
                        {
                          server_was_split = NO;
                          sendto_ops("split-mode deactived by manual override");
                        }
#if defined(SPLIT_PONG)
                      got_server_pong = YES;
#endif
                    }
                }
              else
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY is currently %i",
                             me.name,
                             parv[0],
                             SPLITDELAY/60);
                }
          return 0;
          break;

        case TOKEN_SPLITNUM:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval < SPLIT_SMALLNET_SIZE)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITNUM must be >= %d",
                             me.name, parv[0],SPLIT_SMALLNET_SIZE);
                  return 0;
                }
              sendto_realops("%s has changed SPLITNUM to %i",
                             parv[0], newval);
              SPLITNUM = newval;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPLITNUM is currently %i",
                         me.name,
                         parv[0],
                         SPLITNUM);
            }
          return 0;
          break;

          case TOKEN_SPLITUSERS:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS must be >= 0",
                               me.name, parv[0]);
                    return 0;
                  }
                sendto_realops("%s has changed SPLITUSERS to %i",
                               parv[0], newval);
                SPLITUSERS = newval;
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS is currently %i",
                           me.name,
                           parv[0],
                           SPLITUSERS);
              }
            return 0;
            break;
#endif
#ifdef ANTI_SPAMBOT
          case TOKEN_SPAMNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled ANTI_SPAMBOT",
                                   parv[0]);
                    return 0;
                  }

                if(newval < MIN_SPAM_NUM)
                  SPAMNUM = MIN_SPAM_NUM;
                else
                  SPAMNUM = newval;
                sendto_realops("%s has changed SPAMNUM to %i",
                               parv[0], SPAMNUM);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
                           me.name, parv[0], SPAMNUM);
              }

            return 0;
            break;

        case TOKEN_SPAMTIME:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < MIN_SPAM_TIME)
                SPAMTIME = MIN_SPAM_TIME;
              else
                SPAMTIME = newval;
              sendto_realops("%s has changed SPAMTIME to %i",
                             parv[0], SPAMTIME);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, parv[0], SPAMTIME);
            }
          return 0;
          break;
#endif
        case TOKEN_LOG:
          return 0;
          break;

        default:
        case TOKEN_BAD:
          break;
        }
    }
  sendto_one(sptr, ":%s NOTICE %s :Options: MAX AUTOCONN",
             me.name, parv[0]);
#ifdef FLUD
  sendto_one(sptr, ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
             me.name, parv[0]);
#endif
#ifdef ANTI_DRONE_FLOOD
  sendto_one(sptr, ":%s NOTICE %s :Options: DRONETIME, DRONECOUNT",
             me.name, parv[0]);
#endif
#ifdef ANTI_SPAMBOT
  sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
             me.name, parv[0]);
#endif
#ifdef NEED_SPLITCODE
  sendto_one(sptr, ":%s NOTICE %s :Options: SPLITNUM SPLITUSERS SPLITDELAY",
               me.name, parv[0]);
#endif
#ifdef IDLE_CHECK
  sendto_one(sptr, ":%s NOTICE %s :Options: IDLETIME",
             me.name, parv[0]);
#endif
  sendto_one(sptr, ":%s NOTICE %s :Options: LOG",
             me.name, parv[0]);
  return 0;
}
