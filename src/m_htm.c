/************************************************************************
 *   IRC - Internet Relay Chat, src/m_htm.c
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
 *   $Id: m_htm.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "s_log.h"

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


#define LOADCFREQ 5
/*
 * m_htm - HTM command handler
 * high traffic mode info
 */
int m_htm(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char *command;

  if (!MyClient(sptr) || !IsAdmin(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,
        ":%s NOTICE %s :HTM is %s(%d), %s. Max rate = %dk/s. Current = %.1fk/s",
          me.name, parv[0], LIFESUX ? "ON" : "OFF", LIFESUX,
          NOISYHTM ? "NOISY" : "QUIET",
          LRV, currlife);
  if (parc > 1)
    {
      command = parv[1];
      if (!irccmp(command,"TO"))
        {
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value < 10)
                {
                  sendto_one(sptr, ":%s NOTICE %s :Cannot set LRV < 10!",
                             me.name, parv[0]);
                }
              else
                LRV = new_value;
              sendto_one(sptr, ":%s NOTICE %s :NEW Max rate = %dk/s. Current = %.1fk/s",
                         me.name, parv[0], LRV, currlife);
              sendto_realops("%s!%s@%s set new HTM rate to %dk/s (%.1fk/s current)",
                             parv[0], sptr->username, sptr->host,
                             LRV, currlife);
              /* Log when someone sets a new HTM threshold -malign */
              Log("%s!%s@%s set new HTM rate to %dk/s (%.1fk/s current)",parv[0], sptr->username, sptr->host,LRV, currlife);
            }
          else 
            sendto_one(sptr, ":%s NOTICE %s :LRV command needs an integer parameter",me.name, parv[0]);
        }
      else
        {
          if (!irccmp(command,"ON"))
            {
              LIFESUX = 1;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now ON.", me.name, parv[0]);
              sendto_ops("Entering high-traffic mode: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
              /* Log when we go into High Traffic Mode (HTM) -malign */
              Log("HTM ON forced by %s!%s@%s",parv[0], sptr->username, sptr->host);
              LCF = 30; /* 30s */
            }
          else if (!irccmp(command,"OFF"))
            {
              LIFESUX = 0;
              LCF = LOADCFREQ;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now OFF.", me.name, parv[0]);
              sendto_ops("Resuming standard operation: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
              /* Log when we exit High Traffic Mode (HTM) -malign */
              Log("HTM OFF forced by %s!%s@%s",parv[0], sptr->username, sptr->host);
            }
          else if (!irccmp(command,"QUIET"))
            {
              sendto_ops("HTM is now QUIET");
              NOISYHTM = NO;
            }
          else if (!irccmp(command,"NOISY"))
            {
              sendto_ops("HTM is now NOISY");
              NOISYHTM = YES;
            }
          else
            sendto_one(sptr,
                       ":%s NOTICE %s :Commands are:HTM [ON] [OFF] [TO int] [QUIET] [NOISY]",
                       me.name, parv[0]);
        }
    }
  return 0;
}
