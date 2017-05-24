/************************************************************************
 *   IRC - Internet Relay Chat, src/m_server.c
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
 *   $Id: m_server.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"  /* m_server prototype */
#include "client.h"      /* client struct */
#include "common.h"      /* TRUE bleah */
#include "hash.h"        /* add_to_client_hash_table */
#include "irc_string.h"  /* strncpy_irc */
#include "ircd.h"        /* me */
#include "list.h"        /* make_server */
#include "numeric.h"     /* ERR_xxx */
#include "s_conf.h"      /* struct ConfItem */
#include "s_serv.h"      /* server_estab, check_server, my_name_for_link */
#include "s_stats.h"     /* ServerStats */
#include "scache.h"      /* find_or_add */
#include "send.h"        /* sendto_one */
#include "struct.h"      /* bleah */
#include "s_log.h"

#include <string.h>
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
 * m_server - SERVER message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = serverinfo/hopcount
 *      parv[3] = serverinfo
 */
int m_server(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int              i;
  char             info[REALLEN + 1];
  char*            host;
  struct Client*   acptr;
  struct Client*   bcptr;
  struct ConfItem* aconf;
  int              hop;

  info[0] = '\0';
  /*  inpath = get_client_name(cptr,FALSE); */
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(cptr,"ERROR :No servername");
      return 0;
    }
  hop = 0;
  host = parv[1];
  if (parc > 3 && atoi(parv[2]))
    {
      hop = atoi(parv[2]);
      strncpy_irc(info, parv[3], REALLEN);
      info[REALLEN] = '\0';
    }
  else if (parc > 2)
    {
      /*
       * XXX - hmmmm
       */
      strncpy_irc(info, parv[2], REALLEN);
      info[REALLEN] = '\0';
      if ((parc > 3) && ((i = strlen(info)) < (REALLEN - 2)))
        {
          strcat(info, " ");
          strncat(info, parv[3], REALLEN - i - 2);
          info[REALLEN] = '\0';
        }
    }
  /*
   * July 5, 1997
   * Rewritten to throw away server cruft from users,
   * combined the hostname validity test with
   * cleanup of host name, so a cleaned up hostname
   * can be returned as an error if necessary. - Dianora
   *
   * yes, the if(strlen) below is really needed!! 
   */
  if (strlen(host) > HOSTLEN)
    host[HOSTLEN] = '\0';

  if (IsPerson(sptr))
    {
      /*
       * a USER tries something fishy... ;-)
       */
      if (IsServer(cptr))
        {
		  /* heh, obviously we've never encountered this condition ..
		   * get_client_name returns a little sumpin-sumpin static, we
		   * can't use it twice here. -gnp */
        char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

#ifdef HIDE_SERVERS_IPS
		strcpy(nbuf, get_client_name(sptr,MASK_IP));
#else
		strcpy(nbuf, get_client_name(sptr,SHOW_IP));
#endif

          sendto_realops("SERVER command from remote user %s -- %s is a hacked server",
	                 nbuf, 
#ifdef HIDE_SERVERS_IPS
	                 get_client_name(cptr,MASK_IP));
#else							      
                     get_client_name(cptr,SHOW_IP));
#endif			  
        }
      else
        {
          sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
                     me.name, parv[0], "SERVER");
        }
      return 0;
    }
  else
    {
      /* Lets check for bogus names and clean them up
       * we don't bother cleaning up ones from users, becasuse
       * we will never see them any more - Dianora
       */

      int bogus_server = 0;
      int dots = 0;
      char *s;

      s = host;
      while (*s)
        {
          if (!IsServChar(*s)) {
            bogus_server = 1;
            break;
          }
          if ('.' == *s)
            ++dots;
          ++s;
        }

      if (!dots || bogus_server )
        {
          char clean_host[2 * HOSTLEN + 4];
          sendto_one(sptr,"ERROR :Bogus server name (%s)", 
                     clean_string(clean_host, (const unsigned char *) host, 2 * HOSTLEN));
          return exit_client(cptr, cptr, cptr, "Bogus server name");
        }
    }

  /* 
   * *WHEN* can it be that "cptr != sptr" ????? --msa
   * When SERVER command (like now) has prefix. -avalon
   * 
   * check to see this host even has an N line before bothering
   * anyone about it. Its only a quick sanity test to stop
   * the conference room and win95 ircd dorks. 
   * Sure, it will be redundantly checked again in m_server_estab()
   * *sigh* yes there will be wasted CPU as the conf list will
   * be scanned twice. But how often will this happen?
   * - Dianora
   *
   * This should (will be) be recoded to check the IP is valid as well, 
   * with a pointer to the valid N line conf kept for later, saving
   * an extra lookup.. *sigh* - Dianora
   */
  if (!IsServer(cptr))
    {
      if (find_conf_by_name(host, CONF_NOCONNECT_SERVER) == NULL)
        {
#ifdef WARN_NO_NLINE
#ifdef HIDE_SERVERS_IPS
          sendto_realops("Link %s Server %s dropped, no N: line",
                         get_client_name(cptr, MASK_IP), host);
#else
          sendto_realops("Link %s Server %s dropped, no N: line",
	                 get_client_name(cptr, TRUE), host);
#endif			 
	  Log("Access denied. No N line for server %s",
	  		get_client_name(cptr, TRUE));
#endif
          return exit_client(cptr, cptr, cptr, "NO N line");
        }
    }

  if (MyConnect(cptr) && (GlobalSetOptions.autoconn == 0))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("WARNING AUTOCONN is 0, Closing %s",
                       get_client_name(cptr, MASK_IP));
#else     
      sendto_realops("WARNING AUTOCONN is 0, Closing %s",
                 get_client_name(cptr, TRUE));
#endif		 
      return exit_client(cptr, cptr, cptr, "AUTOCONNS off");
    }

  if ((acptr = find_server(host)))
    {
      /*
       * This link is trying feed me a server that I already have
       * access through another path -- multiple paths not accepted
       * currently, kill this link immediately!!
       *
       * Rather than KILL the link which introduced it, KILL the
       * youngest of the two links. -avalon
       */
      char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
      {
#ifdef HIDE_SERVERS_IPS      
        sendto_realops("Link %s cancelled, server %s already exists",
                   get_client_name(bcptr, MASK_IP), host);
#else
        sendto_realops("Link %s cancelled, server %s already exists",
	                   get_client_name(bcptr, TRUE), host);
#endif			   
        return exit_client(bcptr, bcptr, &me, "Server Exists");
      }
      /*
       * in this case, we are not dropping the link from
       * which we got the SERVER message.  Thus we canNOT
       * `return' yet! -krys
       *
       *
       * get_client_name() can return ptr to static buffer...can't use
       * 2 times in same sendto_ops(), so we have to strcpy one =(
       *  - comstud
       */
#ifdef HIDE_SERVERS_IPS
      strcpy(nbuf, get_client_name(bcptr, MASK_IP));
      sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                nbuf, host, get_client_name(cptr, MASK_IP));
#else
      strcpy(nbuf, get_client_name(bcptr, TRUE));
      sendto_realops("Link %s cancelled, server %s reintroduced by %s",
	                    nbuf, host, get_client_name(cptr, TRUE));
#endif			    
      exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  /* The following if statement would be nice to remove
   * since user nicks never have '.' in them and servers
   * must always have '.' in them. There should never be a 
   * server/nick name collision, but it is possible a capricious
   * server admin could deliberately do something strange.
   *
   * -Dianora
   */
  if ((acptr = find_client(host, NULL)) && acptr != cptr)
    {
      /*
       * Server trying to use the same name as a person. Would
       * cause a fair bit of confusion. Enough to make it hellish
       * for a while and servers to send stuff to the wrong place.
       */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
#ifdef HIDE_SERVERS_IPS
      sendto_ops("Link %s cancelled: Server/nick collision on %s",
                 /* inpath */ get_client_name(cptr,MASK_IP), host);
#else
      sendto_ops("Link %s cancelled: Server/nick collision on %s",
                /* inpath */ get_client_name(cptr,FALSE), host);
#endif		
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  if (IsServer(cptr))
    {
      /*
       * Server is informing about a new server behind
       * this link. Create REMOTE server structure,
       * add it to list and propagate word to my other
       * server links...
       */
      if (parc == 1 || info[0] == '\0')
        {
          sendto_one(cptr, "ERROR :No server info specified for %s", host);
          return 0;
        }

      /*
       * See if the newly found server is behind a guaranteed
       * leaf (L-line). If so, close the link.
       */
      if ((aconf = find_conf_host(cptr->confs, host, CONF_LEAF)) &&
          (!aconf->port || (hop > aconf->port)))
        {
#ifdef HIDE_SERVERS_IPS
          sendto_realops("Leaf-only link %s->%s - Closing",
	             get_client_name(cptr,  MASK_IP),
		     aconf->host ? aconf->host : "*");
#else		     
          sendto_realops("Leaf-only link %s->%s - Closing",
                     get_client_name(cptr,  TRUE),
                     aconf->host ? aconf->host : "*");
#endif		     
          sendto_one(cptr, "ERROR :Leaf-only link, sorry.");
          return exit_client(cptr, cptr, cptr, "Leaf Only");
        }

      if (!(aconf = find_conf_host(cptr->confs, host, CONF_HUB)) ||
          (aconf->port && (hop > aconf->port)) )
        {
#ifdef HIDE_SERVERS_IPS
          sendto_realops("Non-Hub link %s introduced %s(%s).",
                     get_client_name(cptr,  MASK_IP), host,
                     aconf ? (aconf->host ? aconf->host : "*") : "!");
#else		     
          sendto_realops("Non-Hub link %s introduced %s(%s).",
	             get_client_name(cptr, TRUE), host,
		     aconf ? (aconf->host ? aconf->host : "*") : "!");
#endif		     
          sendto_one(cptr, "ERROR :%s has no H: line for %s.",
                     get_client_name(cptr,  TRUE), host);
          return exit_client(cptr, cptr, cptr, "Too many servers");
        }

      acptr = make_client(cptr);
      make_server(acptr);
      acptr->hopcount = hop;
      strncpy_irc(acptr->name, host, HOSTLEN);
      strncpy_irc(acptr->info, info, REALLEN);
      acptr->serv->up = find_or_add(parv[0]);
      acptr->servptr = sptr;

      SetServer(acptr);

      Count.server++;

      add_client_to_list(acptr);
      add_to_client_hash_table(acptr->name, acptr);
      add_client_to_llist(&(acptr->servptr->serv->servers), acptr);

      /*
       * Old sendto_serv_but_one() call removed because we now
       * need to send different names to different servers
       * (domain name matching)
       */
      for (bcptr = serv_cptr_list; bcptr; bcptr = bcptr->next_server_client)
        {
          if (bcptr == cptr)
            continue;
          if (!(aconf = bcptr->serv->nline))
            {
#ifdef HIDE_SERVERS_IPS
              sendto_realops("Lost N-line for %s on %s. Closing",
	                 get_client_name(cptr, MASK_IP), host);
#else			 
              sendto_realops("Lost N-line for %s on %s. Closing",
                         get_client_name(cptr, TRUE), host);
#endif			 
              return exit_client(cptr, cptr, cptr, "Lost N line");
            }
          if (match(my_name_for_link(me.name, aconf), acptr->name))
            continue;

          sendto_one(bcptr, ":%s SERVER %s %d :%s",
                     parv[0], acptr->name, hop + 1, acptr->info);
                         
        }
      
      sendto_realops_flags(FLAGS_EXTERNAL, "Server %s being introduced by %s",
                           acptr->name, sptr->name);
      return 0;
    }

  if (!IsUnknown(cptr) && !IsHandshake(cptr))
    return 0;
  /*
   * A local link that is still in undefined state wants
   * to be a SERVER, or we have gotten here as a result of a connect
   * Check if this is allowed and change status accordingly...
   */

  /* 
   * Reject a direct nonTS server connection if we're TS_ONLY -orabidoo
   */
  if (!DoesTS(cptr))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Link %s dropped, non-Ignite+TS server",
                 get_client_name(cptr, MASK_IP));
#else		 
      sendto_realops("Link %s dropped, non-Ignite+TS server",
                 get_client_name(cptr, TRUE));
#endif		 
      return exit_client(cptr, cptr, cptr, "Non-Ignite+TS server");
    }

  /*
   * if we are connecting (Handshake), we already have the name from the
   * C:line in cptr->name
   */
  strncpy_irc(cptr->name, host, HOSTLEN);
  strncpy_irc(cptr->info, info[0] ? info : me.name, REALLEN);
  cptr->hopcount = hop;

  if (check_server(cptr))
    return server_estab(cptr);

  ++ServerStats->is_ref;
  sendto_ops("Received unauthorized connection from %s.",
              get_client_host(cptr));
  return exit_client(cptr, cptr, cptr, "No C/N conf lines");
}
