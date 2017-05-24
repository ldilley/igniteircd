/************************************************************************
 *   IRC - Internet Relay Chat, src/m_pass.c
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
 *  $Id: m_pass.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"  /* m_pass prototype */
#include "client.h"      /* client struct */
#include "irc_string.h"  /* strncpy_irc */
#include "send.h"        /* sendto_one */
#include "numeric.h"     /* ERR_xxx */
#include "ircd.h"        /* me */

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
 * m_pass() - Added Sat, 4 March 1989
 *
 *
 * m_pass - PASS message handler
 *      parv[0] = sender prefix
 *      parv[1] = password
 *      parv[2] = optional extra version information
 */
int m_pass(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char* password = parc > 1 ? parv[1] : NULL;

  if (EmptyString(password))
    {
      sendto_one(cptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "PASS");
      return 0;
    }
  if (!MyConnect(sptr) || (!IsUnknown(cptr) && !IsHandshake(cptr)))
    {
      sendto_one(cptr, form_str(ERR_ALREADYREGISTRED),
                 me.name, parv[0]);
      return 0;
    }
  strncpy_irc(cptr->passwd, password, PASSWDLEN);
  if (parc > 2)
    {
      /* 
       * It looks to me as if orabidoo wanted to have more
       * than one set of option strings possible here...
       * i.e. ":AABBTS" as long as TS was the last two chars
       * however, as we are now using CAPAB, I think we can
       * safely assume if there is a ":TS" then its a TS server
       * -Dianora
       */

       /* Needed to modify the string searched for when a server connects. Originally it was "TS" to detect the ":TS" sent from the other server
          to determine if it did TS or not. Now it has changed to prevent non-Ignite servers from connecting due to certain weirdness that already
          has been tried and tested when for example, a Hybrid server connects to an Ignite server. The weirdness that occurs are mode desynchs because
          of Global Operators being able to change modes within a channel without having ChanOp privileges. These modes are not propogated to non-Ignite
          servers and hence clients from both servers will NOT be experiencing what they should. So while it would be nice to be compatible with Hybrid,
          it appears we cannot with the new code that's in place. -malign */
      /*if ((0 == irccmp(parv[2], "TS")) && (cptr->tsinfo == 0))*/
        if ((0 == irccmp(parv[2], "TS+Ignite")) && (cptr->tsinfo == 0))
        cptr->tsinfo = TS_DOESTS;
    }
  return 0;
}
