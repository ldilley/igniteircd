/************************************************************************
 *   IRC - Internet Relay Chat, src/m_svinfo.c
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
 *   $Id: m_svinfo.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "common.h"     /* TRUE bleah */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"

#include <assert.h>
#include <time.h>
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
 * m_svinfo - SVINFO message handler
 *      parv[0] = sender prefix
 *      parv[1] = TS_CURRENT for the server
 *      parv[2] = TS_MIN for the server
 *      parv[3] = server is standalone or connected to non-TS only
 *      parv[4] = server's idea of UTC time
 */
int m_svinfo(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  time_t deltat;
  time_t theirtime;

  if (MyConnect(sptr) && IsUnknown(sptr))
    return exit_client(sptr, sptr, sptr, "Need SERVER before SVINFO");

  if (!IsServer(sptr) || !MyConnect(sptr) || parc < 5)
    return 0;

  if (TS_CURRENT < atoi(parv[2]) || atoi(parv[1]) < TS_MIN)
    {
      /*
       * a server with the wrong TS version connected; since we're
       * TS_ONLY we can't fall back to the non-TS protocol so
       * we drop the link  -orabidoo
       */
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Link %s dropped, wrong TS protocol version (%s,%s)",
      		 get_client_name(sptr, MASK_IP), parv[1], parv[2]);
#else		 
      sendto_realops("Link %s dropped, wrong TS protocol version (%s,%s)",
                 get_client_name(sptr, TRUE), parv[1], parv[2]);
#endif		 
      return exit_client(sptr, sptr, sptr, "Incompatible TS version");
    }

  sptr->serv->tsversion = atoi(parv[1]);
  
  /*
   * since we're here, might as well set CurrentTime while we're at it
   */
  CurrentTime = time(0);
  theirtime = atol(parv[4]);
  deltat = abs(theirtime - CurrentTime);

  if (deltat > TS_MAX_DELTA)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops(
       "Link %s dropped, excessive TS delta (my TS=%d, their TS=%d, delta=%d)",
       		 get_client_name(sptr, MASK_IP), CurrentTime, theirtime,deltat);
#else		 
      sendto_realops(
       "Link %s dropped, excessive TS delta (my TS=%d, their TS=%d, delta=%d)",
                 get_client_name(sptr, TRUE), CurrentTime, theirtime,deltat);
#endif		 
      return exit_client(sptr, sptr, sptr, "Excessive TS delta");
    }

  if (deltat > TS_WARN_DELTA)
    { 
#ifdef HIDE_SERVERS_IPS
      sendto_realops(
      		 "Link %s notable TS delta (my TS=%d, their TS=%d, delta=%d)",
		 get_client_name(sptr, MASK_IP), CurrentTime, theirtime, deltat);
#else		 
      sendto_realops(
                 "Link %s notable TS delta (my TS=%d, their TS=%d, delta=%d)",
                 get_client_name(sptr, TRUE), CurrentTime, theirtime, deltat);
#endif		 
    }

  return 0;
}
