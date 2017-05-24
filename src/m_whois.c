/************************************************************************
 *   IRC - Internet Relay Chat, src/m_who.c
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
 *   $Id: m_whois.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "channel.h"
#include "hash.h"
#include "struct.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "list.h"
#include "irc_string.h"

#include <string.h>

static char buf[BUFSIZE];

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
** m_whois
**      parv[0] = sender prefix
**      parv[1] = nickname masklist
*/
int     m_whois(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  static anUser UnknownUser =
  {
    NULL,       /* next */
    NULL,       /* channel */
    NULL,       /* invited */
    NULL,       /* away */
    0,          /* last */
    1,          /* refcount */
    0,          /* joined */
    "<Unknown>"         /* server */
  };
  Link  *lp;
  anUser        *user;
  struct Client *acptr, *a2cptr;
  aChannel *chptr;
  char  *nick, *name;
  /* char  *tmp; */
  char  *p = NULL;
  int   found, len, mlen;
  static time_t last_used=0L;
  int found_mode;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }

  if(parc > 2)
    {
      if (hunt_server(cptr,sptr,":%s WHOIS %s :%s", 1,parc,parv) !=
          HUNTED_ISME)
        return 0;
      parv[1] = parv[2];
    }

  if(!IsEmpowered(sptr) && !MyConnect(sptr)) /* pace non local requests */
    {
      if((last_used + WHOIS_WAIT) > CurrentTime)
        {
          /* Unfortunately, returning anything to a non local
           * request =might= increase sendq to be usable in a split hack
           * Sorry gang ;-( - Dianora
           */
          return 0;
        }
      else
        {
          last_used = CurrentTime;
        }
    }

  /* Multiple whois from remote hosts, can be used
   * to flood a server off. One could argue that multiple whois on
   * local server could remain. Lets think about that, for now
   * removing it totally. 
   * -Dianora 
   */

  /*  for (tmp = parv[1]; (nick = strtoken(&p, tmp, ",")); tmp = NULL) */
  nick = parv[1];
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';

    {
      int       invis, showperson, member, wilds;
      found = 0;
      (void)collapse(nick);
      wilds = (strchr(nick, '?') || strchr(nick, '*'));
      /*
      ** We're no longer allowing remote users to generate
      ** requests with wildcards.
      */
      if (wilds)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], nick);
          return 0;
        }
      /*        continue; */

      /* If the nick doesn't have any wild cards in it,
       * then just pick it up from the hash table
       * - Dianora 
       */

      if(!wilds)
        {
          acptr = hash_find_client(nick,(struct Client *)NULL);
          if(!acptr)
            {
              sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                         me.name, parv[0], nick);
              return 0;
              /*              continue; */
            }
          if(!IsPerson(acptr))
            {
              sendto_one(sptr, form_str(RPL_ENDOFWHOIS),
                         me.name, parv[0], parv[1]);
              return 0;
            }
            /*      continue; */

          user = acptr->user ? acptr->user : &UnknownUser;
          name = (!*acptr->name) ? "?" : acptr->name;
          invis = IsInvisible(acptr);
          member = (user->channel) ? 1 : 0;

          a2cptr = find_server(user->server);

          sendto_one(sptr, form_str(RPL_WHOISUSER), me.name,
                     parv[0], name,
                     acptr->username,
                     acptr->host,
                     acptr->info);

          mlen = strlen(me.name) + strlen(parv[0]) + 6 +
            strlen(name);
          for (len = 0, *buf = '\0', lp = user->channel; lp;
               lp = lp->next)
            {
              chptr = lp->value.chptr;
              if (ShowChannel(sptr, chptr))
                {
                  if (len + strlen(chptr->chname)
                      > (size_t) BUFSIZE - 4 - mlen)
                    {
                      sendto_one(sptr,
                                 ":%s %d %s %s :%s",
                                 me.name,
                                 RPL_WHOISCHANNELS,
                                 parv[0], name, buf);
                      *buf = '\0';
                      len = 0;
                    }

		  found_mode = user_channel_mode(acptr, chptr);
#ifdef HIDE_OPS
		  if(is_chan_op(sptr,chptr))
#endif
		    {
                      /* if (found_mode & CHFL_ADMIN)
                        *(buf + len++) = '*'; */
		      if (found_mode & CHFL_CHANOP)
			*(buf + len++) = '@';
		      else if (found_mode & CHFL_VOICE)
			*(buf + len++) = '+';
		    }
                  if (len)
                    *(buf + len) = '\0';
                  (void)strcpy(buf + len, chptr->chname);
                  len += strlen(chptr->chname);
                  (void)strcat(buf + len, " ");
                  len++;
                }
            }
          if (buf[0] != '\0')
            sendto_one(sptr, form_str(RPL_WHOISCHANNELS),
                       me.name, parv[0], name, buf);
         
#ifdef SERVERHIDE
          if (!(IsEmpowered(sptr) || acptr == sptr))
            sendto_one(sptr, form_str(RPL_WHOISSERVER),
                       me.name, parv[0], name, NETWORK_NAME,
                       NETWORK_DESC);
          else
#endif
          sendto_one(sptr, form_str(RPL_WHOISSERVER),
                     me.name, parv[0], name, user->server,
                     a2cptr?a2cptr->info:"*Not On This Net*");

          if (user->away)
            sendto_one(sptr, form_str(RPL_AWAY), me.name,
                       parv[0], name, user->away);

#ifdef SERVICES
          /* If nickname is registered with NickServ, display it as such -malign */
          if (IsNickReg(acptr))
            sendto_one(sptr, form_str(RPL_WHOISREGNICK),
                       me.name, parv[0], name);
#endif /* SERVICES */

          /* If user is a legacy Global Operator, give them the label Administrator -malign */
          if (IsAdmin(acptr))
            sendto_one(sptr, form_str(RPL_WHOISADMIN),
                       me.name, parv[0], name);

          if (IsOper(acptr))
            sendto_one(sptr, form_str(RPL_WHOISOPERATOR),
                       me.name, parv[0], name);

#ifdef WHOIS_NOTICE
          if ((MyAdmin(acptr)) && ((acptr)->umodes & FLAGS_SPY) &&
              (MyConnect(sptr)) && (IsPerson(sptr)) && (acptr != sptr))
            sendto_one(acptr,
                       ":%s NOTICE %s :*** Notice -- %s (%s@%s) is doing a /whois on you.",
                       me.name, acptr->name, parv[0], sptr->username,
                       sptr->host);
#endif /* #ifdef WHOIS_NOTICE */

          if ((acptr->user
#ifdef SERVERHIDE
              && IsEmpowered(sptr)
#endif
              && MyConnect(acptr)))
            sendto_one(sptr, form_str(RPL_WHOISIDLE),
                       me.name, parv[0], name,
                       CurrentTime - user->last,
                       acptr->firsttime);
          sendto_one(sptr, form_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);
          return 0;
          /*      continue; */
        }

      /* wild is true so here we go */

      for (acptr = GlobalClientList; (acptr = next_client(acptr, nick));
           acptr = acptr->next)
        {
          if (IsServer(acptr))
            continue;
          /*
           * I'm always last :-) and acptr->next == NULL!!
           */
          if (IsMe(acptr))
            break;
          /*
           * 'Rules' established for sending a WHOIS reply:
           *
           *
           * - if wildcards are being used dont send a reply if
           *   the querier isnt any common channels and the
           *   client in question is invisible and wildcards are
           *   in use (allow exact matches only);
           *
           * - only send replies about common or public channels
           *   the target user(s) are on;
           */

/* If its an unregistered client, ignore it, it can
   be "seen" on a /trace anyway  -Dianora */

          if(!IsRegistered(acptr))
            continue;

          user = acptr->user ? acptr->user : &UnknownUser;
          name = (!*acptr->name) ? "?" : acptr->name;
          invis = IsInvisible(acptr);
          member = (user->channel) ? 1 : 0;
          showperson = (wilds && !invis && !member) || !wilds;
          for (lp = user->channel; lp; lp = lp->next)
            {
              chptr = lp->value.chptr;
              member = IsMember(sptr, chptr);
              if (invis && !member)
                continue;
              if (member || (!invis && PubChannel(chptr)))
                {
                  showperson = 1;
                  break;
                }
              if (!invis && HiddenChannel(chptr) &&
                  !SecretChannel(chptr))
                showperson = 1;
            }
          if (!showperson)
            continue;
          
          a2cptr = find_server(user->server);
          
          sendto_one(sptr, form_str(RPL_WHOISUSER), me.name,
                     parv[0], name,
                     acptr->username, acptr->host, acptr->info);
          found = 1;
          mlen = strlen(me.name) + strlen(parv[0]) + 6 +
            strlen(name);
          for (len = 0, *buf = '\0', lp = user->channel; lp;
               lp = lp->next)
            {
              chptr = lp->value.chptr;
              if (ShowChannel(sptr, chptr))
                {
                  if (len + strlen(chptr->chname)
                      > (size_t) BUFSIZE - 4 - mlen)
                    {
                      sendto_one(sptr,
                                 ":%s %d %s %s :%s",
                                 me.name,
                                 RPL_WHOISCHANNELS,
                                 parv[0], name, buf);
                      *buf = '\0';
                      len = 0;
                    }
		  found_mode = user_channel_mode(acptr, chptr);
#ifdef HIDE_OPS
                  if(is_chan_op(sptr,chptr))
#endif
		     {
                       /* if (found_mode & CHFL_ADMIN)
                         *(buf + len++) = '*'; */
                       if (found_mode & CHFL_CHANOP)
			 *(buf + len++) = '@';
		       else if (found_mode & CHFL_VOICE)
			 *(buf + len++) = '+';
		     }
                  if (len)
                    *(buf + len) = '\0';
                  (void)strcpy(buf + len, chptr->chname);
                  len += strlen(chptr->chname);
                  (void)strcat(buf + len, " ");
                  len++;
                }
            }
          if (buf[0] != '\0')
            sendto_one(sptr, form_str(RPL_WHOISCHANNELS),
                       me.name, parv[0], name, buf);
         
#ifdef SERVERHIDE
          if (!(IsEmpowered(sptr) || acptr == sptr))
            sendto_one(sptr, form_str(RPL_WHOISSERVER),
                       me.name, parv[0], name, NETWORK_NAME,
                       NETWORK_DESC);
          else    
#endif
          sendto_one(sptr, form_str(RPL_WHOISSERVER),
                     me.name, parv[0], name, user->server,
                     a2cptr?a2cptr->info:"*Not On This Net*");

          if (user->away)
            sendto_one(sptr, form_str(RPL_AWAY), me.name,
                       parv[0], name, user->away);

#ifdef SERVICES
          /* If nickname is registered with NickServ, display it as such -malign */
          if (IsNickReg(acptr))
            sendto_one(sptr, form_str(RPL_WHOISREGNICK),
                       me.name, parv[0], name);
#endif /* SERVICES */

          /* If user is a legacy Global Operator, give them the label Administrator -malign */
          if (IsAdmin(acptr))
            sendto_one(sptr, form_str(RPL_WHOISADMIN),
                       me.name, parv[0], name);

          if (IsOper(acptr))
            sendto_one(sptr, form_str(RPL_WHOISOPERATOR),
                       me.name, parv[0], name);

#ifdef WHOIS_NOTICE
          if ((MyAdmin(acptr)) && ((acptr)->umodes & FLAGS_SPY) &&
              (MyConnect(sptr)) && (IsPerson(sptr)) && (acptr != sptr))
            sendto_one(acptr,
                       ":%s NOTICE %s :*** Notice -- %s (%s@%s) is doing a /whois on you.",
                       me.name, acptr->name, parv[0], sptr->username,
                       sptr->host);
#endif /* #ifdef WHOIS_NOTICE */

          if ((acptr->user
#ifdef SERVERHIDE
              && IsEmpowered(sptr) 
#endif                 
              && MyConnect(acptr)))
            sendto_one(sptr, form_str(RPL_WHOISIDLE),
                       me.name, parv[0], name,
                       CurrentTime - user->last,
                       acptr->firsttime);
        }
      if (!found)
        sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                   me.name, parv[0], nick);
      /*
      if (p)
        p[-1] = ',';
        */
    }
  sendto_one(sptr, form_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);
  
  return 0;
}
