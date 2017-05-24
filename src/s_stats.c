/************************************************************************
 *   IRC - Internet Relay Chat, src/s_stats.c
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
 *  $Id: s_stats.c,v 1.1.1.1 2006/03/08 23:28:12 malign Exp $
 */

#include "s_stats.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_bsd.h"
#include "send.h"

#include <string.h>

/*
 * stats stuff
 */
static struct ServerStatistics  ircst;
struct ServerStatistics* ServerStats = &ircst;

void init_stats()
{
  memset(&ircst, 0, sizeof(ircst));
}

void tstats(struct Client *cptr, const char *name)
{
  struct Client*           acptr;
  int                      i;
  struct ServerStatistics* sp;
  struct ServerStatistics  tmp;

  sp = &tmp;
  memcpy(sp, ServerStats, sizeof(struct ServerStatistics));
  for (i = 0; i < highest_fd; i++)
    {
      if (!(acptr = local[i]))
        continue;
      if (IsServer(acptr))
        {
          sp->is_sbs += acptr->sendB;
          sp->is_sbr += acptr->receiveB;
          sp->is_sks += acptr->sendK;
          sp->is_skr += acptr->receiveK;
          sp->is_sti += CurrentTime - acptr->firsttime;
          sp->is_sv++;
          if (sp->is_sbs > 1023)
            {
              sp->is_sks += (sp->is_sbs >> 10);
              sp->is_sbs &= 0x3ff;
            }
          if (sp->is_sbr > 1023)
            {
              sp->is_skr += (sp->is_sbr >> 10);
              sp->is_sbr &= 0x3ff;
            }
          
        }
      else if (IsClient(acptr))
        {
          sp->is_cbs += acptr->sendB;
          sp->is_cbr += acptr->receiveB;
          sp->is_cks += acptr->sendK;
          sp->is_ckr += acptr->receiveK;
          sp->is_cti += CurrentTime - acptr->firsttime;
          sp->is_cl++;
          if (sp->is_cbs > 1023)
            {
              sp->is_cks += (sp->is_cbs >> 10);
              sp->is_cbs &= 0x3ff;
            }
          if (sp->is_cbr > 1023)
            {
              sp->is_ckr += (sp->is_cbr >> 10);
              sp->is_cbr &= 0x3ff;
            }
          
        }
      else if (IsUnknown(acptr))
        sp->is_ni++;
    }

  sendto_one(cptr, ":%s %d %s :accepts %u refused %u",
             me.name, RPL_STATSDEBUG, name, sp->is_ac, sp->is_ref);
  sendto_one(cptr, ":%s %d %s :unknown commands %u prefixes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_unco, sp->is_unpf);
  sendto_one(cptr, ":%s %d %s :nick collisions %u unknown closes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_kill, sp->is_ni);
  sendto_one(cptr, ":%s %d %s :wrong direction %u empty %u",
             me.name, RPL_STATSDEBUG, name, sp->is_wrdi, sp->is_empt);
  sendto_one(cptr, ":%s %d %s :numerics seen %u mode fakes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_num, sp->is_fake);
  sendto_one(cptr, ":%s %d %s :auth successes %u fails %u",
             me.name, RPL_STATSDEBUG, name, sp->is_asuc, sp->is_abad);
  sendto_one(cptr, ":%s %d %s :local connections %u udp packets %u",
             me.name, RPL_STATSDEBUG, name, sp->is_loc, sp->is_udp);
  sendto_one(cptr, ":%s %d %s :Client Server",
             me.name, RPL_STATSDEBUG, name);
  sendto_one(cptr, ":%s %d %s :connected %u %u",
             me.name, RPL_STATSDEBUG, name, sp->is_cl, sp->is_sv);
  sendto_one(cptr, ":%s %d %s :bytes sent %u.%uK %u.%uK",
             me.name, RPL_STATSDEBUG, name,
             sp->is_cks, sp->is_cbs, sp->is_sks, sp->is_sbs);
  sendto_one(cptr, ":%s %d %s :bytes recv %u.%uK %u.%uK",
             me.name, RPL_STATSDEBUG, name,
             sp->is_ckr, sp->is_cbr, sp->is_skr, sp->is_sbr);
  sendto_one(cptr, ":%s %d %s :time connected %u %u",
             me.name, RPL_STATSDEBUG, name, sp->is_cti, sp->is_sti);
#ifdef FLUD
  sendto_one(cptr, ":%s %d %s :CTCP Floods Blocked %u",
             me.name, RPL_STATSDEBUG, name, sp->is_flud);
#endif /* FLUD */
#ifdef ANTI_IP_SPOOF
  sendto_one(cptr, ":%s %d %s :IP Spoofers %u",
             me.name, RPL_STATSDEBUG, name, sp->is_ipspoof);
#endif /* ANTI_IP_SPOOF */
}
