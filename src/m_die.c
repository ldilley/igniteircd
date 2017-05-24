/************************************************************************
 *   IRC - Internet Relay Chat, src/m_version.c
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
 *   $Id: m_die.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_log.h"
#include "send.h"
#include <stdlib.h>

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
 * m_die - DIE command handler
 */
int m_die(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client* acptr;
  int      i;

  if (!MyClient(sptr) || !IsEmpowered(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (!IsOperDie(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no D flag", me.name, parv[0]);
      return 0;
    }

  if (parc < 2)
    {
      sendto_one(sptr,":%s NOTICE %s :Need server name: /die %s",
                 me.name,sptr->name,me.name);
      return 0;
    }
  else
    {
      if (irccmp(parv[1], me.name))
        {
          sendto_one(sptr,":%s NOTICE %s :Mismatch on /die %s",
                     me.name,sptr->name,me.name);
          return 0;
        }
    }

  for (i = 0; i <= highest_fd; i++)
    {
      if (!(acptr = local[i]))
        continue;
      if (IsClient(acptr))
        {
          if(IsEmpowered(acptr))
            sendto_one(acptr,
                       ":%s NOTICE %s :Server Terminating. %s",
                       me.name, acptr->name,
                       get_client_name(sptr, HIDE_IP));
          else
            sendto_one(acptr,
                       ":%s NOTICE %s :Server Terminating. %s",
                       me.name, acptr->name,
                       get_client_name(sptr, MASK_IP));
        }
      else if (IsServer(acptr))
        sendto_one(acptr, ":%s ERROR :Terminated by %s",
                   me.name, get_client_name(sptr, MASK_IP));
    }
  flush_connections(0);
  Log("DIE issued by %s.", get_client_name(sptr, HIDE_IP));
  /* 
   * this is a normal exit, tell the os it's ok 
   */
  exit(0);
  /* NOT REACHED */
  return 0;
}

