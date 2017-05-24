/************************************************************************
 *   IRC - Internet Relay Chat, src/m_squit.c
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
 *   $Id: m_squit.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "common.h"      /* FALSE bleah */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"

#include <assert.h>

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
 * m_squit - SQUIT message handler
 *      parv[0] = sender prefix
 *      parv[1] = server name
 *      parv[2] = comment
 */
int m_squit(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct ConfItem* aconf;
  char*            server;
  struct Client*   acptr;
  char  *comment = (parc > 2 && parv[2]) ? parv[2] : cptr->name;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      server = parv[1];
      /*
      ** To accomodate host masking, a squit for a masked server
      ** name is expanded if the incoming mask is the same as
      ** the server name for that link to the name of link.
      */
      while ((*server == '*') && IsServer(cptr))
        {
          aconf = cptr->serv->nline;
          if (!aconf)
            break;
          if (!irccmp(server, my_name_for_link(me.name, aconf)))
            server = cptr->name;
          break; /* WARNING is normal here */
          /* NOTREACHED */
        }
      /*
      ** The following allows wild cards in SQUIT. Only useful
      ** when the command is issued by an oper.
      */
      for (acptr = GlobalClientList; (acptr = next_client(acptr, server));
           acptr = acptr->next)
        if (IsServer(acptr) || IsMe(acptr))
          break;
      if (acptr && IsMe(acptr))
        {
          acptr = cptr;
          server = cptr->name;
        }
    }
  else
    {
      /*
      ** This is actually protocol error. But, well, closing
      ** the link is very proper answer to that...
      **
      ** Closing the client's connection probably wouldn't do much
      ** good.. any oper out there should know that the proper way
      ** to disconnect is /QUIT :)
      **
      ** its still valid if its not a local client, its then
      ** a protocol error for sure -Dianora
      */
      if(MyClient(sptr))
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "SQUIT");
          return 0;
        }
      else
        {
          server = cptr->host;
          acptr = cptr;
        }
    }

  /*
  ** SQUIT semantics is tricky, be careful...
  **
  ** The old (irc2.2PL1 and earlier) code just cleans away the
  ** server client from the links (because it is never true
  ** "cptr == acptr".
  **
  ** This logic here works the same way until "SQUIT host" hits
  ** the server having the target "host" as local link. Then it
  ** will do a real cleanup spewing SQUIT's and QUIT's to all
  ** directions, also to the link from which the orinal SQUIT
  ** came, generating one unnecessary "SQUIT host" back to that
  ** link.
  **
  ** One may think that this could be implemented like
  ** "hunt_server" (e.g. just pass on "SQUIT" without doing
  ** nothing until the server having the link as local is
  ** reached). Unfortunately this wouldn't work in the real life,
  ** because either target may be unreachable or may not comply
  ** with the request. In either case it would leave target in
  ** links--no command to clear it away. So, it's better just
  ** clean out while going forward, just to be sure.
  **
  ** ...of course, even better cleanout would be to QUIT/SQUIT
  ** dependant users/servers already on the way out, but
  ** currently there is not enough information about remote
  ** clients to do this...   --msa
  */
  if (!acptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
                 me.name, parv[0], server);
      return 0;
    }
  if (IsOper(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (MyClient(sptr) && !IsOperRemote(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag",me.name,parv[0]);
      return 0;
    }

  /*
  **  Notify all opers, if my local link is remotely squitted
  */
  if (MyConnect(acptr) && !IsEmpowered(cptr))
    {
      sendto_ops_butone(NULL, &me,
                        ":%s WALLOPS :Received SQUIT %s from %s (%s)",
                        me.name, server, get_client_name(sptr,FALSE), comment);
      Log("SQUIT issued by %s : %s (%s)", parv[0], server, comment);
    }
  else if (MyConnect(acptr))
    sendto_ops("Received SQUIT %s from %s (%s)",
               acptr->name, get_client_name(sptr,FALSE), comment);
  
  return exit_client(cptr, acptr, sptr, comment);
}

