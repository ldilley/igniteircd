/************************************************************************
 *   IRC - Internet Relay Chat, src/m_message.c
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
 *   $Id: m_message.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "flud.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"

#include "msg.h"  /* ZZZ should be able to remove this */
#include "channel.h"
#include "irc_string.h"
#include "hash.h"
#include "class.h"

#include <string.h>

static void
send_oper_special (struct Client *cptr, struct Client *sptr,
		    char *cmd, char *nick, char *message);

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
** m_message (used in m_private() and m_notice())
** the general function to deliver MSG's between users/channels
**
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
*/

static  int     m_message(struct Client *cptr,
                          struct Client *sptr,
                          int parc,
                          char *parv[],
                          int notice)
{
  struct Client       *acptr;
  struct Channel *chptr;
  char  *nick, *server, *cmd, *host;
  char *p;
  int type=0;

  cmd = notice ? MSG_NOTICE : MSG_PRIVATE;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NORECIPIENT),
                 me.name, parv[0], cmd);
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  if (MyConnect(sptr))
    {
#ifdef ANTI_SPAMBOT
#ifndef ANTI_SPAMBOT_WARN_ONLY
      /* if its a spambot, just ignore it */
      if(sptr->join_leave_count >= MAX_JOIN_LEAVE_COUNT)
        return 0;
#endif
#endif
      parv[1] = canonize(parv[1]);
    }

  /*
  ** channels are privmsg'd a lot more than other clients, moved up here
  ** plain old channel msg ?
  */
  p = NULL;

  for (nick = strtoken(&p, parv[1], ","); nick;
       nick = strtoken(&p, NULL, ","))
    {
      /* This slows us down :-( especially opers...
       * but disambiguates as best we can
       * the old "is it an oper send to a channel or to a hostmask?" bug -db
       */
      if (IsEmpowered(sptr) && (*nick == '#'))
	{
	  if ((chptr = hash_find_channel(nick, NullChn)) == NULL)
	    {
	      send_oper_special(cptr, sptr, cmd, nick, parv[2]);
	      continue;
	    }
	  else
	    {
	      if (!IsMember(sptr, chptr)) /* oper send to hash ! */
		{
		  send_oper_special(cptr, sptr, cmd, nick, parv[2]);
		  continue;
		}
	    }
	}

      if (IsChanPrefix(*nick)
	  && (IsPerson(sptr) && (chptr = hash_find_channel(nick, NullChn))))
	{
#ifdef  IDLE_CHECK
	  /* reset idle time for message only if target exists */
	  if(MyClient(sptr) && sptr->user)
	    sptr->user->last = CurrentTime;
#endif
#ifdef FLUD
	  if(!notice)
	    if(check_for_ctcp(parv[2]))
	      check_for_flud(sptr, NULL, chptr, 1);
#endif /* FLUD */

	  if (can_send(sptr, chptr) == 0)
	    sendto_channel_butone(cptr, sptr, chptr,
				  ":%s %s %s :%s",
				  parv[0], cmd, nick,
				  parv[2]);
	  else if (!notice)
	    sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
		       me.name, parv[0], nick);
	  continue;
	}
      
      /*
      ** @# type of channel msg?
      */

      /* if (*nick == '*')
        type = MODE_ADMIN; */
      if(*nick == '@')
	type = MODE_CHANOP;
      else if(*nick == '+')
	type = MODE_CHANOP|MODE_VOICE;
      
      if(type)
	{
	  /* Strip if using DALnet chanop/voice prefix. */
	  /* if (*(nick+1) == '*' || *(nick+1) == '@' || *(nick+1) == '+') */
          if (*(nick+1) == '@' || *(nick+1) == '+')
	    {
	      nick++;
	      *nick = '@';
	      type = MODE_CHANOP|MODE_VOICE;
	    }

	  /* suggested by Mortiis */
	  if(!*nick)        /* if its a '\0' dump it, there is no recipient */
	    {
	      sendto_one(sptr, form_str(ERR_NORECIPIENT),
			 me.name, parv[0], cmd);
	      return -1;
	    }

	  if (!IsPerson(sptr))      /* This means, servers can't send messages */
	    return -1;

	  /* At this point, nick+1 should be a channel name i.e. #foo or &foo
	   * if the channel is found, fine, if not report an error
	   */

	  if ( (chptr = hash_find_channel(nick+1, NullChn)) )
	    {
#ifdef  IDLE_CHECK
	      /* reset idle time for message only if target exists */
	      if(MyClient(sptr) && sptr->user)
		sptr->user->last = CurrentTime;
#endif
#ifdef FLUD
	      if(!notice)
		if(check_for_ctcp(parv[2]))
		  check_for_flud(sptr, NULL, chptr, 1);
#endif /* FLUD */

	      if (!is_chan_op(sptr,chptr) && !has_voice(sptr,chptr))
		{
		  if (!notice)
		      sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN),
				 me.name, parv[0], nick);
		  continue;
		}
	      else
		{
		  sendto_channel_type(cptr, sptr, chptr, type, nick+1,
				      cmd, parv[2]);
		}
	    }
	  else
	    {
	      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
			 me.name, parv[0], nick);
	      continue;
	    }
	  return 0;
	}

      /*
      ** nickname addressed?
      */
      if ((acptr = find_person(nick, NULL)))
	{
#ifdef  IDLE_CHECK
	  /* reset idle time for message only if target exists */
	  if(MyClient(sptr) && sptr->user)
	    sptr->user->last = CurrentTime;
#endif
#ifdef FLUD
	  if(!notice && MyConnect(acptr))
	    if(check_for_ctcp(parv[2]))
	      if(check_for_flud(sptr, acptr, NULL, 1))
		return 0;
#endif
#ifdef ANTI_DRONE_FLOOD
	  if(MyConnect(acptr) && IsClient(sptr) && !IsEmpowered(sptr) && DRONETIME)
	    {
	      if((acptr->first_received_message_time+DRONETIME) < CurrentTime)
		{
		  acptr->received_number_of_privmsgs=1;
		  acptr->first_received_message_time = CurrentTime;
		  acptr->drone_noticed = 0;
		}
	      else
		{
		  if(acptr->received_number_of_privmsgs > DRONECOUNT)
		    {
		      if(acptr->drone_noticed == 0) /* tiny FSM */
			{
			  sendto_ops_flags(FLAGS_BOTS,
					   "Possible Drone Flooder %s [%s@%s] on %s target: %s",
					   sptr->name, sptr->username,
					   sptr->host,
					   sptr->user->server, acptr->name);
			  acptr->drone_noticed = 1;
			}
		      /* heuristic here, if target has been getting a lot
		       * of privmsgs from clients, and sendq is above halfway up
		       * its allowed sendq, then throw away the privmsg, otherwise
		       * let it through. This adds some protection, yet doesn't
		       * DOS the client.
		       * -Dianora
		       */
		      if(DBufLength(&acptr->sendQ) > (get_sendq(acptr)/2L))
			{
			  if(acptr->drone_noticed == 1) /* tiny FSM */
			    {
			      sendto_ops_flags(FLAGS_BOTS,
					       "ANTI_DRONE_FLOOD SendQ protection activated for %s",
					       acptr->name);
			      
			      sendto_one(acptr,     
					 ":%s NOTICE %s :*** Notice -- Server drone flood protection activated for %s",
					 me.name, acptr->name, acptr->name);
			      acptr->drone_noticed = 2;
			    }
			}

		      if(DBufLength(&acptr->sendQ) <= (get_sendq(acptr)/4L))
			{
			  if(acptr->drone_noticed == 2)
			    {
			      sendto_one(acptr,     
					 ":%s NOTICE %s :*** Notice -- Server drone flood protection de-activated for %s",
					 me.name, acptr->name, acptr->name);
			      acptr->drone_noticed = 1;
			    }
			}
		      if(acptr->drone_noticed > 1)
			return 0;
		    }
		  else
		    acptr->received_number_of_privmsgs++;
		}
	    }
#endif
	  if (!notice && MyConnect(sptr) &&
	      acptr->user && acptr->user->away)
	    sendto_one(sptr, form_str(RPL_AWAY), me.name,
		       parv[0], acptr->name,
		       acptr->user->away);
	  sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
			    parv[0], cmd, nick, parv[2]);

#ifdef  IDLE_CHECK
	  /* reset idle time for message only if its not to self */
	  if (sptr != acptr)
	    {
	      if(sptr->user)
		sptr->user->last = CurrentTime;
	    }
#endif
	  continue;
	}

      /* Everything below here should be reserved for opers 
       * as pointed out by Mortiis, user%host.name@server.name 
       * syntax could be used to flood without FLUD protection
       * its also a delightful way for non-opers to find users who
       * have changed nicks -Dianora
       *
       * Grrr it was pointed out to me that x@service is valid
       * for non-opers too, and wouldn't allow for flooding/stalking
       * -Dianora
       */
      /*
      ** the following two cases allow masks in NOTICEs
      ** (for OPERs only)
      **
      ** Armin, 8Jun90 (gruner@informatik.tu-muenchen.de)
      */
      if (*nick == '$')
	{
	  if(!IsEmpowered(sptr))
	    {
	      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
			 me.name, parv[0], nick);
	      return -1;
	    }
	  send_oper_special(cptr, sptr, cmd, nick, parv[2]);
	  continue;
	}
        
      /*
      ** user[%host]@server addressed?
      */
      if ((server = (char *)strchr(nick, '@')) &&
	  (acptr = find_server(server + 1)))
	{
	  int count = 0;

	  /* Disable the user%host@server form for non-opers
	   * -Dianora
	   */

	  if( (char *)strchr(nick,'%') && !IsEmpowered(sptr))
	    {
	      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
			 me.name, parv[0], nick);
	      continue;
	    }
        
	  /*
	  ** Not destined for a user on me :-(
	  */
	  if (!IsMe(acptr))
	    {
	      sendto_one(acptr,":%s %s %s :%s", parv[0],
			 cmd, nick, parv[2]);
	      continue;
	    }

	  *server = '\0';

	  /* special case opers@server */
	  if(!irccmp(nick,"opers") && IsEmpowered(sptr))
	    {
	      sendto_realops("To opers: From %s: %s",sptr->name,parv[2]);
	      continue;
	    }
        
	  if ((host = (char *)strchr(nick, '%')))
	    *host++ = '\0';

	  /*
	  ** Look for users which match the destination host
	  ** (no host == wildcard) and if one and one only is
	  ** found connected to me, deliver message!
	  */
	  acptr = find_userhost(nick, host, NULL, &count);
	  if (server)
	    *server = '@';
	  if (host)
	    *--host = '%';
	  if (acptr)
	    {
	      if (count == 1)
		sendto_prefix_one(acptr, sptr,
				  ":%s %s %s :%s",
				  parv[0], cmd, nick, parv[2]);
	      else if (!notice)
		sendto_one(sptr,
			   form_str(ERR_TOOMANYTARGETS),
			   me.name, parv[0], nick);
	    }
	  if (acptr)
	    continue;
	}
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name,
		 parv[0], nick);
    }
  return 0;
}


static void
send_oper_special (struct Client *cptr, struct Client *sptr,
		   char *cmd, char *nick, char *message)
{
  char *s;

  if ((s = strrchr(nick, '.')) == NULL)
    {
      sendto_one(sptr, form_str(ERR_NOTOPLEVEL),
		 me.name, sptr->name, nick);
      return;
    }

  while (*++s)
    if (*s == '.' || *s == '*' || *s == '?')
      break;

  if (*s == '*' || *s == '?')
    {
      sendto_one(sptr, form_str(ERR_WILDTOPLEVEL),
		 me.name, sptr->name, nick);
      return;
    }

  /* here we cope with $$server, $server and $#host.mask */
  if (*nick == '$')
    {
      if((*(nick+1) == '$') || (*(nick+1) == '#'))
        sendto_match_butone(IsServer(cptr) ? cptr : NULL, 
			  sptr, nick + 2,
			  (*(nick+1) == '$') ? MATCH_SERVER : MATCH_HOST,
			  ":%s %s %s :%s", sptr->name,
			  cmd, nick, message);
      else
        sendto_match_butone(IsServer(cptr) ? cptr : NULL,
	                  sptr, nick + 1, MATCH_SERVER,
			  ":%s %s %s :%s",
			  sptr->name, cmd, nick, message);
    }
  else
    {
      /* sanity test, by now, this code can only deal with #host.mask */
      if (*nick != '#')
	return;

      sendto_match_butone(IsServer(cptr) ? cptr : NULL, 
			  sptr, nick + 1,
			  MATCH_HOST,
			  ":%s %s %s :%s", sptr->name,
			  cmd, nick, message);
    }
}

/*
** m_private
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = message text
*/

int     m_private(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  return m_message(cptr, sptr, parc, parv, 0);
}

/*
** m_notice
**      parv[0] = sender prefix
**      parv[1] = receiver list
**      parv[2] = notice text
*/

int     m_notice(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  return m_message(cptr, sptr, parc, parv, 1);
}
