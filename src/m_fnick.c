/************************************************************************
*   IRC - Internet Relay Chat, src/m_fnick.c
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
* $Id: m_fnick.c,v 1.6 2007/02/14 10:00:43 malign Exp $
*/

#include "m_commands.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "m_gline.h"
#include "numeric.h"
#include "res.h"
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "whowas.h"

#include <string.h>

/* m_fnick - Force nick change upon client -malign */
/* parv[0] - Sender prefix */
/* parv[1] - Current nick */
/* parv[2] - New nick */

int m_fnick(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{

#ifndef FNICK
sendto_one(sptr,"FNICK is not enabled."); /* Inform user issuing command that it is not compiled in */
#else

  struct Client *acptr=NULL;
  struct Channel *chptr=NULL;

     /* Ensure we are given the proper number of arguments -malign */
  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "FNICK");
      return 0;
    }

     /* If the person attempting to use FNICK is not local or an IRC Admin, don't even bother -malign */
  if (!MyClient(sptr) || !IsAdmin(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

     /* Verify person exists first -malign */
  if (!(acptr = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, parv[0], parv[1]);
      return 0;
    }

     /* Verify the nick is not in use -malign */
  if ((acptr = find_server(parv[1])))
      {
        sendto_one(sptr, form_str(ERR_NICKNAMEINUSE), me.name, parv[0], parv[1]);
        return 0;
      }
 
    /* Ensure new nickname given is a valid one -malign */
  if (!clean_nick_name(parv[2]))
    {
      sendto_one(sptr, ":%s NOTICE %s :Invalid nickname.", me.name, parv[0]);
      return 0;
    }

     /* If client is not mine, bail -malign */
  if (!MyClient(acptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :%s is not local to our server.", me.name, parv[0], acptr->name);
      return 0;
    }

     /* If the target is an IRC Administrator, then bail -malign */
  if (IsAdmin(acptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :Unable to FNICK IRC Administrators.", me.name, parv[0]);
      return 0;
    }

     /* Ensure user issuing command is an IRC Administrator -malign */
  if (IsAdmin(sptr))
    {
      /* When NICK is given, the new nickname already passes through the clean_nick_name function so it'll be legit -malign */
      sendto_common_channels(acptr, ":%s NICK :%s", parv[1], parv[2]);

      if (IsPerson(acptr))
        {
          add_history(acptr, 1);
        }

      sendto_serv_butone(acptr, "%s NICK %s :%lu", parv[1], parv[2], acptr->tsinfo);

      if (acptr->name[0])
        {
          (void)del_from_client_hash_table(acptr->name, acptr);
        }

      (void)strcpy(acptr->name, parv[2]);
      (void)add_to_client_hash_table(parv[2], acptr);

      sendto_one(acptr,":%s NICK :%s", parv[1], parv[2]);
      sendto_one(acptr,":%s NOTICE %s :Administrator %s has changed your nickname from %s to %s.", me.name, acptr->name, sptr->name, parv[1], parv[2]);
      sendto_realops("ADMIN %s (%s@%s) used FNICK to change nickname %s (%s@%s) to %s.",
                    sptr->name,sptr->username,sptr->host,parv[1],acptr->username,acptr->host,parv[2]);

      Log("ADMIN %s (%s@%s) used FNICK to change nickname %s (%s@%s) to %s.",
                    sptr->name,sptr->username,sptr->host,parv[1],acptr->username,acptr->host,parv[2]);

      sendto_match_servs(chptr, acptr, ":%s NICK :%s", parv[1], parv[2]);
     }

#endif /* FNICK */

  return 0;
}
