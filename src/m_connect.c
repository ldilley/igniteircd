/************************************************************************
 *   IRC - Internet Relay Chat, src/m_connect.c
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
 *   $Id: m_connect.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "common.h"     /* FALSE bleah */
#include "ircd.h"
#include "irc_string.h"
#include "numeric.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"

#include <assert.h>
#include <stdlib.h>     /* atoi */

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
 * m_connect - CONNECT command handler
 * 
 * Added by Jto 11 Feb 1989
 *
 * m_connect
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = port number
 *      parv[3] = remote server
 */
int m_connect(struct Client* cptr, struct Client* sptr, int parc, char* parv[])
{
  
  int              port;
  int              tmpport;
  struct ConfItem* aconf;
  struct Client*   acptr;
  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return -1;
    }

  if (IsOper(sptr) && parc > 3) {
    /* 
     * Only allow LocOps to make local CONNECTS --SRB
     */
    return 0;
  }

  if (MyConnect(sptr) && !IsOperRemote(sptr) && parc > 3)
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag", me.name, parv[0]);
      return 0;
    }

  if (hunt_server(cptr, sptr,
                  ":%s CONNECT %s %s :%s", 3, parc, parv) != HUNTED_ISME)
    return 0;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "CONNECT");
      return -1;
    }

  if ((acptr = find_server(parv[1])))
    {
      sendto_one(sptr, ":%s NOTICE %s :Connect: Server %s %s %s.",
                 me.name, parv[0], parv[1], "already exists from",
                 acptr->from->name);
      return 0;
    }

  /*
   * try to find the name, then host, if both fail notify ops and bail
   */
  if (!(aconf = find_conf_by_name(parv[1], CONF_CONNECT_SERVER))) {
#ifndef HIDE_SERVERS_IPS  
    if (!(aconf = find_conf_by_host(parv[1], CONF_CONNECT_SERVER))) {
#endif    
      sendto_one(sptr,
                 "NOTICE %s :Connect: Host %s not listed in ircd.conf",
                 parv[0], parv[1]);
      return 0;
#ifndef HIDE_SERVERS_IPS      
    }
#endif    
  }
  assert(0 != aconf);
  /*
   * Get port number from user, if given. If not specified,
   * use the default form configuration structure. If missing
   * from there, then use the precompiled default.
   */
  tmpport = port = aconf->port;
  if (parc > 2 && !EmptyString(parv[2]))
    {
      if ((port = atoi(parv[2])) <= 0)
        {
          sendto_one(sptr, "NOTICE %s :Connect: Illegal port number",
                     parv[0]);
          return 0;
        }
    }
  else if (port <= 0 && (port = PORTNUM) <= 0)
    {
      sendto_one(sptr, ":%s NOTICE %s :Connect: missing port number",
                 me.name, parv[0]);
      return 0;
    }
  /*
   * Notify all operators about remote connect requests
   */
  if (!IsEmpowered(cptr))
    {
      sendto_ops_butone(NULL, &me,
                        ":%s WALLOPS :Remote CONNECT %s %s from %s",
                        me.name, parv[1], parv[2] ? parv[2] : "",
                        get_client_name(sptr, FALSE));

      Log("CONNECT from %s : %s %s", 
          parv[0], parv[1], parv[2] ? parv[2] : "");
    }

  aconf->port = port;
  /*
   * at this point we should be calling connect_server with a valid
   * C:line and a valid port in the C:line
   */
  if (connect_server(aconf, sptr, NULL))
#if (defined SERVERHIDE) || (defined HIDE_SERVERS_IPS)
    sendto_one(sptr, ":%s NOTICE %s :*** Connecting to %s[%s].%d",
               me.name, parv[0], "255.255.255.255", aconf->name, aconf->port);
  else
    sendto_one(sptr, ":%s NOTICE %s :*** Couldn't connect to %s.%d",
               me.name, parv[0], "255.255.255.255",aconf->port);
#else
    sendto_one(sptr, ":%s NOTICE %s :*** Connecting to %s[%s].%d",
               me.name, parv[0], aconf->host, aconf->name, aconf->port);
  else
    sendto_one(sptr, ":%s NOTICE %s :*** Couldn't connect to %s.%d",
               me.name, parv[0], aconf->host,aconf->port);
#endif
  /*
   * client is either connecting with all the data it needs or has been
   * destroyed
   */
  aconf->port = tmpport;
  return 0;
}
