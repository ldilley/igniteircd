/************************************************************************
 *   IRC - Internet Relay Chat, src/m_ison.c
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
 *   $Id: m_ison.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"

#include <string.h>

static char buf[BUFSIZE];

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
 * m_ison added by Darren Reed 13/8/91 to act as an efficent user indicator
 * with respect to cpu/bandwidth used. Implemented for NOTIFY feature in
 * clients. Designed to reduce number of whois requests. Can process
 * nicknames in batches as long as the maximum buffer length.
 *
 * format:
 * ISON :nicklist
 */
/*
 * Take care of potential nasty buffer overflow problem 
 * -Dianora
 *
 */


int m_ison(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client *acptr;
  char *current_nick;
  char *current_insert_point;
  char *end_of_current_nick;
  int len;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "ISON");
      return 0;
    }

  ircsprintf(buf, form_str(RPL_ISON), me.name, parv[0]);
  len = strlen(buf);
  current_insert_point = buf + len;

  if (!IsAdmin(cptr))
    cptr->priority +=20; /* this keeps it from moving to 'busy' list */

  current_nick = parv[1];

  end_of_current_nick = strchr(current_nick,' ');

  for (;;)
    {
      if(end_of_current_nick)
        *end_of_current_nick = '\0';
      if ((acptr = find_person(current_nick, NULL)))
	{
	  len = strlen(acptr->name);
	  if( (current_insert_point + (len + 5)) < (buf + sizeof(buf)) )
	    {
	      memcpy((void *)current_insert_point,
		     (void *)acptr->name, len);
	      current_insert_point += len;
	      *current_insert_point++ = ' ';
	    }
	  else
	    break;
	}

      if(!end_of_current_nick)
        break;

      current_nick = end_of_current_nick;
      current_nick++;  /* should skip to next '\0' or non ' ' */
      end_of_current_nick = strchr(current_nick,' ');
    }

/*  current_insert_point--; Do NOT take out the trailing space, it breaks ircII --Rodder */
  *current_insert_point = '\0';
  sendto_one(sptr, "%s", buf);
  return 0;
}
