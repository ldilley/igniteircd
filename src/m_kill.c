/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kill.c
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
 *   $Id: m_kill.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */

#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "whowas.h"
#include "irc_string.h"

#include <string.h>

static char buf[BUFSIZE], buf2[BUFSIZE];

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
** m_kill
**      parv[0] = sender prefix
**      parv[1] = kill victim
**      parv[2] = kill path
*/
int m_kill(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client*    acptr;
  const char* inpath = cptr->name;
  char*       user;
  char*       path;
  char*       killer;
  char*       reason;
  int         chasing = 0;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KILL");
      return 0;
    }

  user = parv[1];
  path = parv[2]; /* Either defined or NULL (parc >= 2!!) */

  if (!IsPrivileged(cptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (MyClient(sptr) && IsEmpowered(sptr) && !IsSetOperK(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return 0;
    }

  if (IsEmpowered(cptr))
    {
      if (!BadPtr(path))
        if (strlen(path) > (size_t) KILLLEN)
          path[KILLLEN] = '\0';
    }

  if (!(acptr = find_client(user, NULL)))
    {
      /*
      ** If the user has recently changed nick, we automaticly
      ** rewrite the KILL for this new nickname--this keeps
      ** servers in synch when nick change and kill collide
      */
      if (!(acptr = get_history(user, (long)KILLCHASETIMELIMIT)))
        {
          sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                     me.name, parv[0], user);
          return 0;
        }
      sendto_one(sptr,":%s NOTICE %s :KILL changed from %s to %s",
                 me.name, parv[0], user, acptr->name);
      chasing = 1;
    }
  if (!MyConnect(acptr) && IsOper(cptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if (IsServer(acptr) || IsMe(acptr))
    {
      sendto_one(sptr, form_str(ERR_CANTKILLSERVER),
                 me.name, parv[0]);
      return 0;
    }

  if (MyAdmin(sptr) && !MyConnect(acptr) && (!IsOperGlobalKill(sptr)))
    {
      sendto_one(sptr, ":%s NOTICE %s :Nick %s is not on your server.",
                 me.name, parv[0], acptr->name);
      return 0;
    }

  if (IsAdmin(acptr))
    {
     /* Disallow the use of KILL against IRC Administrators -malign */
     sendto_one(sptr,"Unable to KILL IRC Administrators.");
     return 0;
    }

  if (!IsServer(cptr))
    {
      /*
      ** The kill originates from this server, initialize path.
      ** (In which case the 'path' may contain user suplied
      ** explanation ...or some nasty comment, sigh... >;-)
      **
      **        ...!operhost!oper
      **        ...!operhost!oper (comment)
      */
      inpath = cptr->host;
      if (!BadPtr(path))
        {
          ircsprintf(buf, "%s!%s%s (%s)",
                     cptr->username, cptr->name,
                     IsAdmin(sptr) ? "" : "(L)", path);
          path = buf;
          reason = path;
        }
      else
        path = cptr->name;
    }
  else if (BadPtr(path))
    path = "*no-path*"; /* Bogus server sending??? */
  /*
  ** Notify all *local* opers about the KILL (this includes the one
  ** originating the kill, if from this server--the special numeric
  ** reply message is not generated anymore).
  **
  ** Note: "acptr->name" is used instead of "user" because we may
  **     have changed the target because of the nickname change.
  */
  if (IsOper(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if(BadPtr(parv[2]))
    {
      reason = sptr->name;
    }
  else
    {
      if(IsEmpowered(cptr))
        {
          reason = parv[2];
        }
      else
        {
          reason = strchr(parv[2], ' ');
	  if(reason)
	    reason++;
	  else
	    reason = parv[2];
	}
    }

  if (IsEmpowered(sptr)) /* send it normally */
    {
      sendto_realops("Received KILL message for %s. From %s Path: %s!%s", 
               acptr->name, parv[0], inpath, path);
      /*
       * dilema here: we don't want non opers to see pathes which
       * contain real IP addresses.  But we do want opers to see them.
       * The choices are currently to do two sends, or just not show kills
       * to non opers.  I'm chosing the latter for now.  --Rodder
         sendto_ops("Received KILL message for %s. From %s!%s@%s Path:supressed.!%s",
                    acptr->name, sptr->name, sptr->username, sptr->host,
                    reason);
       */
    }
  else
    sendto_realops_flags(FLAGS_SKILL,
                     "Received KILL message for %s. From %s",
                     acptr->name, parv[0]);

  /* Log the KILL to ircd.log -malign */
  Log("KILL %s from %s.",acptr->name, parv[0]);

  /*
  ** And pass on the message to other servers. Note, that if KILL
  ** was changed, the message has to be sent to all links, also
  ** back.
  ** Suicide kills are NOT passed on --SRB
  */
  if (!MyConnect(acptr) || !MyConnect(sptr) || !IsEmpowered(sptr))
    {
      sendto_serv_butone(cptr, ":%s KILL %s :%s!%s",
                         parv[0], acptr->name, inpath, path);
      if (chasing && IsServer(cptr))
        sendto_one(cptr, ":%s KILL %s :%s!%s",
                   me.name, acptr->name, inpath, path);
      acptr->flags |= FLAGS_KILLED;
    }

  /*
  ** Tell the victim she/he has been zapped, but *only* if
  ** the victim is on current server--no sense in sending the
  ** notification chasing the above kill, it won't get far
  ** anyway (as this user don't exist there any more either)
  */
  if (MyConnect(acptr))
    sendto_prefix_one(acptr, sptr,":%s KILL %s :%s",
                      parv[0], acptr->name, reason);

  /* XXX old code showed too much */
  /*
    sendto_prefix_one(acptr, sptr,":%s KILL %s :%s!%s",
                      parv[0], acptr->name, inpath, path);
    */

  /*
  ** Set FLAGS_KILLED. This prevents exit_one_client from sending
  ** the unnecessary QUIT for this. (This flag should never be
  ** set in any other place)
  */
  if (MyConnect(acptr) && MyConnect(sptr) && IsEmpowered(sptr))
#ifdef SERVERHIDE
    ircsprintf(buf2, "Killed (%s (%s))",
#else
    ircsprintf(buf2, "Local kill by %s (%s)",
#endif
                     sptr->name,
                     BadPtr(parv[2]) ? sptr->name : parv[2]);
  else
    {
      if ((killer = strchr(path, ' ')))
        {
          while (*killer && *killer != '!')
            killer--;
          if (!*killer)
            killer = path;
          else
            killer++;
        }
      else
        killer = path;
      ircsprintf(buf2, "Killed (%s)", killer);
    }
  return exit_client(cptr, acptr, sptr, buf2);
}
