/************************************************************************
 *   IRC - Internet Relay Chat, src/m_away.c
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
 *   $Id: m_away.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"

#include <stdlib.h>
#include <string.h>

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


/***********************************************************************
 * m_away() - Added 14 Dec 1988 by jto. 
 *            Not currently really working, I don't like this
 *            call at all...
 *
 *            ...trying to make it work. I don't like it either,
 *            but perhaps it's worth the load it causes to net.
 *            This requires flooding of the whole net like NICK,
 *            USER, MODE, etc messages...  --msa
 ***********************************************************************/

/*
** m_away
**      parv[0] = sender prefix
**      parv[1] = away message
*/
int     m_away(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char  *away, *awy2 = parv[1];

  /* make sure the user exists */
  if (!(sptr->user))
    {
      sendto_realops_flags(FLAGS_DEBUG,
                           "Got AWAY from nil user, from %s (%s)\n",cptr->name,sptr->name);
      return 0;
    }

  away = sptr->user->away;

  if (parc < 2 || !*awy2)
    {
      /* Marking as not away */
      
      if (away)
        {
          MyFree(away);
          sptr->user->away = NULL;
        }
/* some lamers scripts continually do a /away, hence making a lot of
   unnecessary traffic. *sigh* so... as comstud has done, I've
   commented out this sendto_serv_butone() call -Dianora */
/*      sendto_serv_butone(cptr, ":%s AWAY", parv[0]); */
      if (MyConnect(sptr))
        sendto_one(sptr, form_str(RPL_UNAWAY),
                   me.name, parv[0]);
      return 0;
    }

  /* Marking as away */
  
  if (strlen(awy2) > (size_t) TOPICLEN)
    awy2[TOPICLEN] = '\0';
/* some lamers scripts continually do a /away, hence making a lot of
   unnecessary traffic. *sigh* so... as comstud has done, I've
   commented out this sendto_serv_butone() call -Dianora */
/*  sendto_serv_butone(cptr, ":%s AWAY :%s", parv[0], awy2); */

  /* don't use realloc() -Dianora */

  if (away)
    MyFree(away);

  away = (char *)MyMalloc(strlen(awy2)+1);
  strcpy(away,awy2);

  sptr->user->away = away;

  if (MyConnect(sptr))
    sendto_one(sptr, form_str(RPL_NOWAWAY), me.name, parv[0]);
  return 0;
}
