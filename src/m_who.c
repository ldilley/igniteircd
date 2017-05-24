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
 *   $Id: m_who.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
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

static  void    do_who(struct Client *sptr,
                       struct Client *acptr,
                       struct Channel *repchan,
                       Link *lp)
{
  char  status[5];
  /* Using a pointer will compile faster than an index */
  char *p = status;

  if (acptr->user->away)
    *p++ = 'G';
  else
    *p++ = 'H';
  if (IsEmpowered(acptr))
    *p++ = '*';
  if ((repchan != NULL) && (lp == NULL))
    lp = find_user_link(repchan->members, acptr);
  if (lp != NULL)
    {
#ifdef HIDE_OPS
      if(is_chan_op(sptr,repchan))
#endif
	{
          /* if (lp->flags & CHFL_ADMIN)
            *p++ = '*'; */
       	  if (lp->flags & CHFL_CHANOP)
	    *p++ = '@';
	  else if (lp->flags & CHFL_VOICE)
	    *p++ = '+';
	}
    }
  *p = '\0';

  sendto_one(sptr, form_str(RPL_WHOREPLY), me.name, sptr->name,
             (repchan) ? (repchan->chname) : "*",
             acptr->username,
             acptr->host,
#ifdef SERVERHIDE
             IsEmpowered(sptr) ? acptr->user->server : NETWORK_NAME,
#else
             acptr->user->server,
#endif
             acptr->name,
             status,
             acptr->hopcount,
             acptr->info);
}

/*
** m_who
**      parv[0] = sender prefix
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag, only 'o' for now.
*/
int     m_who(struct Client *cptr,
              struct Client *sptr,
              int parc,
              char *parv[])
{
  struct Client *acptr;
  char  *mask = parc > 1 ? parv[1] : NULL;
  Link  *lp;
  struct Channel *chptr;
  struct Channel *mychannel = NULL;
  char  *channame = NULL;
  int   oper = parc > 2 ? (*parv[2] == 'o' ): 0; /* Show OPERS only */
  int   member;
  int   maxmatches = 500;

  mychannel = NullChn;
  if (sptr->user)
    if ((lp = sptr->user->channel))
      mychannel = lp->value.chptr;

  /*
  **  Following code is some ugly hacking to preserve the
  **  functions of the old implementation. (Also, people
  **  will complain when they try to use masks like "12tes*"
  **  and get people on channel 12 ;) --msa
  */
  if (mask)
    (void)collapse(mask);
  if (!mask || (*mask == (char) 0))
    {
      sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0],
                 EmptyString(mask) ?  "*" : mask);
      return 0;
    }
  else if ((*(mask+1) == (char) 0) && (*mask == '*'))
    {
      if (!mychannel)
        {
          sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0],
                     EmptyString(mask) ?  "*" : mask);
          return 0;
        }
      channame = mychannel->chname;
    }
  else
    channame = mask;

  if (IsChannelName(channame))
    {
      /*
       * List all users on a given channel
       */
      chptr = hash_find_channel(channame, NULL);
      if (chptr)
        {
          member = IsMember(sptr, chptr);
          if (member || !SecretChannel(chptr))
            for (lp = chptr->members; lp; lp = lp->next)
              {
                if (oper && !IsEmpowered(lp->value.cptr))
                  continue;
                if (IsInvisible(lp->value.cptr) && !member)
                  continue;
                do_who(sptr, lp->value.cptr, chptr, lp);
              }
        }
    }
  else if (mask && 
           ((acptr = find_client(mask, NULL)) != NULL) &&
           IsPerson(acptr) && (!oper || IsEmpowered(acptr)))
    {
      int isinvis = 0;
      struct Channel *ch2ptr = NULL;

      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
        {
          chptr = lp->value.chptr;
          member = IsMember(sptr, chptr);
          if (isinvis && !member)
            continue;
          if (member || (!isinvis && PubChannel(chptr)))
            {
              ch2ptr = chptr;
              break;
            }
        }
      do_who(sptr, acptr, ch2ptr, NULL);
    }
  else for (acptr = GlobalClientList; acptr; acptr = acptr->next)
    {
      struct Channel *ch2ptr = NULL;
      int       showperson, isinvis;

      if (!IsPerson(acptr))
        continue;
      if (oper && !IsEmpowered(acptr))
        continue;
      
      showperson = 0;
      /*
       * Show user if they are on the same channel, or not
       * invisible and on a non secret channel (if any).
       * Do this before brute force match on all relevant fields
       * since these are less cpu intensive (I hope :-) and should
       * provide better/more shortcuts - avalon
       */
      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
        {
          chptr = lp->value.chptr;
          member = IsMember(sptr, chptr);
          if (isinvis && !member)
            continue;
          if (member || (!isinvis && PubChannel(chptr)))
            {
              ch2ptr = chptr;
              showperson = 1;
              break;
            }
          if (HiddenChannel(chptr) && !SecretChannel(chptr) &&
              !isinvis)
            showperson = 1;
        }
      if (!acptr->user->channel && !isinvis)
        showperson = 1;
      if (showperson &&
          (!mask ||
           match(mask, acptr->name) ||
           match(mask, acptr->username) ||
           match(mask, acptr->host) ||
           match(mask, acptr->user->server) ||
           match(mask, acptr->info)))
        {
          do_who(sptr, acptr, ch2ptr, NULL);
          if (!--maxmatches)
            {
              sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0],
                         EmptyString(mask) ?  "*" : mask);
              return 0;
            }
        }
    }

  sendto_one(sptr, form_str(RPL_ENDOFWHO), me.name, parv[0],
             EmptyString(mask) ?  "*" : mask);
  return 0;
}
