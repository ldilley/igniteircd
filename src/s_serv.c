/*   IRC - Internet Relay Chat, src/s_serv.c
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
 *   $Id: s_serv.c,v 1.1.1.1 2006/03/08 23:28:12 malign Exp $
 */

#include "s_serv.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "res.h"
#include "struct.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_stats.h"
#include "s_user.h"
#include "s_zip.h"
#include "scache.h"
#include "send.h"
#include "s_debug.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount     = 1;

/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name        cap     */ 
#ifdef ZIP_LINKS
  { "ZIP",      CAP_ZIP },
#endif
  { "QS",       CAP_QS },
  { "EX",       CAP_EX },
  { "CHW",      CAP_CHW },
  { "KNOCK",    CAP_KNOCK },
  { 0,   0 }
};


/*
 * my_name_for_link - return wildcard name of my server name 
 * according to given config entry --Jto
 * XXX - this is only called with me.name as name
 */
const char* my_name_for_link(const char* name, aConfItem* aconf)
{
  static char          namebuf[HOSTLEN + 1];
  int         count = aconf->port;
  const char* start = name;

  if (count <= 0 || count > 5)
    return start;

  while (count-- && name)
    {
      name++;
      name = strchr(name, '.');
    }
  if (!name)
    return start;

  namebuf[0] = '*';
  strncpy_irc(&namebuf[1], name, HOSTLEN - 1);
  namebuf[HOSTLEN] = '\0';
  return namebuf;
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int hunt_server(struct Client *cptr, struct Client *sptr, char *command,
                int server, int parc, char *parv[])
{
  struct Client *acptr = NULL;
  int wilds;

  /*
   * Assume it's me, if no server
   */
  if (parc <= server || BadPtr(parv[server]) ||
      match(me.name, parv[server]) ||
      match(parv[server], me.name))
    return (HUNTED_ISME);
  /*
   * These are to pickup matches that would cause the following
   * message to go in the wrong direction while doing quick fast
   * non-matching lookups.
   */
#ifdef SERVERHIDE
  if ( !(MyClient(sptr) && !IsAdmin(sptr))
      && (acptr = find_client(parv[server], NULL)) )
#else
  if ((acptr = find_client(parv[server], NULL)))
#endif
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;
  if (!acptr && (acptr = find_server(parv[server])))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;

  collapse(parv[server]);
  wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

  /*
   * Again, if there are no wild cards involved in the server
   * name, use the hash lookup
   * - Dianora
   */
  if (!acptr)
    {
#ifdef SERVERHIDE
      if ((!wilds || (MyClient(sptr) && !IsAdmin(sptr))))
#else
      if (!wilds)
#endif
        {
          if (!(acptr = find_server(parv[server])))
            {
              sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
                         parv[0], parv[server]);
              return(HUNTED_NOSUCH);
            }
        }
      else
        {
          for (acptr = GlobalClientList;
               (acptr = next_client(acptr, parv[server]));
               acptr = acptr->next)
            {
              if (acptr->from == sptr->from && !MyConnect(acptr))
                continue;
              /*
               * Fix to prevent looping in case the parameter for
               * some reason happens to match someone from the from
               * link --jto
               */
              if (IsRegistered(acptr) && (acptr != cptr))
                break;
            }
        }
    }

  if (acptr)
    {
      if (IsMe(acptr) || MyClient(acptr))
        return HUNTED_ISME;
      if (!match(acptr->name, parv[server]))
        parv[server] = acptr->name;
      sendto_one(acptr, command, parv[0],
                 parv[1], parv[2], parv[3], parv[4],
                 parv[5], parv[6], parv[7], parv[8]);
      return(HUNTED_PASS);
    } 
  sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
             parv[0], parv[server]);
  return(HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
time_t try_connections(time_t currenttime)
{
  struct ConfItem*   aconf;
  struct Client*     cptr;
  int                connecting = FALSE;
  int                confrq;
  time_t             next = 0;
  struct Class*      cltmp;
  struct ConfItem*   con_conf = NULL;
  int                con_class = 0;

  Debug((DEBUG_NOTICE,"Connection check at: %s", myctime(currenttime)));

  for (aconf = ConfigItemList; aconf; aconf = aconf->next )
    {
      /*
       * Also when already connecting! (update holdtimes) --SRB 
       */
      if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
        continue;
      cltmp = ClassPtr(aconf);
      /*
       * Skip this entry if the use of it is still on hold until
       * future. Otherwise handle this entry (and set it on hold
       * until next time). Will reset only hold times, if already
       * made one successfull connection... [this algorithm is
       * a bit fuzzy... -- msa >;) ]
       */
      if (aconf->hold > currenttime)
        {
          if (next > aconf->hold || next == 0)
            next = aconf->hold;
          continue;
        }

      if ((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ )
        confrq = MIN_CONN_FREQ;

      aconf->hold = currenttime + confrq;
      /*
       * Found a CONNECT config with port specified, scan clients
       * and see if this server is already connected?
       */
      cptr = find_server(aconf->name);
      
      if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
          (!connecting || (ClassType(cltmp) > con_class)))
        {
          con_class = ClassType(cltmp);
          con_conf = aconf;
          /* We connect only one at time... */
          connecting = TRUE;
        }
      if ((next > aconf->hold) || (next == 0))
        next = aconf->hold;
    }

  if (0 == GlobalSetOptions.autoconn)
    {
      /*
       * auto connects disabled, send message to ops and bail
       */
      if (connecting)
#ifdef HIDE_SERVERS_IP
        sendto_ops("Connection to %s not activated.", con_conf->name);
#else
        sendto_ops("Connection to %s[%s] not activated.",
                 con_conf->name, con_conf->host);
#endif
      sendto_ops("WARNING AUTOCONN is 0, autoconns are disabled");
      Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
      return next;
    }

  if (connecting)
    {
      if (con_conf->next)  /* are we already last? */
        {
          struct ConfItem**  pconf;
          for (pconf = &ConfigItemList; (aconf = *pconf);
               pconf = &(aconf->next))
            /* 
             * put the current one at the end and
             * make sure we try all connections
             */
            if (aconf == con_conf)
              *pconf = aconf->next;
          (*pconf = con_conf)->next = 0;
        }
      if (!(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
        {
#ifdef HIDE_SERVERS_IPS
          sendto_realops("Connection to %s not activated, autoconn is off.",
			 con_conf->name);
          sendto_realops("WARNING AUTOCONN on %s is disabled",
			 con_conf->name);
#else
          sendto_realops("Connection to %s[%s] not activated, autoconn is off.",
			 con_conf->name, con_conf->host);
          sendto_realops("WARNING AUTOCONN on %s[%s] is disabled",
			 con_conf->name, con_conf->host);
#endif
        }
      else
        {
#ifdef HIDE_SERVERS_IPS
          if (connect_server(con_conf, 0, 0))
            sendto_realops("Connection to %s activated.",
			   con_conf->name);
#else
          if (connect_server(con_conf, 0, 0))
            sendto_realops("Connection to %s[%s] activated.",
			   con_conf->name, con_conf->host);
#endif
        }
    }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
  return next;
}

/*
 * check_server - check access for a server given its name 
 * (passed in cptr struct). Must check for all C/N lines which have a 
 * name which matches the name given and a host which matches. A host 
 * alias which is the same as the server name is also acceptable in the 
 * host field of a C/N line.
 *  
 *  0 = Access denied
 *  1 = Success
 */
int check_server(struct Client* cptr)
{
  struct SLink*    lp;
  struct ConfItem* c_conf = 0;
  struct ConfItem* n_conf = 0;

  assert(0 != cptr);

  if (attach_confs(cptr, cptr->name, 
                   CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER ) < 2)
    {
      Debug((DEBUG_DNS,"No C/N lines for %s", cptr->name));
      return 0;
    }
  lp = cptr->confs;
#if 0  
  if (cptr->dns_query)
    {
      int             i;
      struct hostent* hp   = cptr->dns_reply->hp;
      char*           name = hp->h_name;
      /*
       * if we are missing a C or N line from above, search for
       * it under all known hostnames we have for this ip#.
       */
      for (i = 0, name = hp->h_name; name; name = hp->h_aliases[i++])
        {
          if (!c_conf)
            c_conf = find_conf_host(lp, name, CONF_CONNECT_SERVER );
          if (!n_conf)
            n_conf = find_conf_host(lp, name, CONF_NOCONNECT_SERVER );
          if (c_conf && n_conf)
            {
              strncpy_irc(cptr->host, name, HOSTLEN);
              break;
            }
        }
      for (i = 0; hp->h_addr_list[i]; ++i)
        {
          if (!c_conf)
            c_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_CONNECT_SERVER);
          if (!n_conf)
            n_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_NOCONNECT_SERVER);
        }
    }
#endif
  /*
   * Check for C and N lines with the hostname portion the ip number
   * of the host the server runs on. This also checks the case where
   * there is a server connecting from 'localhost'.
   */
  if (!c_conf)
    c_conf = find_conf_host(lp, cptr->host, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_host(lp, cptr->host, CONF_NOCONNECT_SERVER);
  /*
   * Attach by IP# only if all other checks have failed.
   * It is quite possible to get here with the strange things that can
   * happen when using DNS in the way the irc server does. -avalon
   */
  if (!c_conf)
    c_conf = find_conf_ip(lp, (char*)& cptr->ip,
                          cptr->username, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_ip(lp, (char*)& cptr->ip,
                          cptr->username, CONF_NOCONNECT_SERVER);
  /*
   * detach all conf lines that got attached by attach_confs()
   */
  det_confs_butmask(cptr, 0);
  /*
   * if no C or no N lines, then deny access
   */
  if (!c_conf || !n_conf)
    {
      Debug((DEBUG_DNS, "sv_cl: access denied: [%s@%s] c %x n %x",
             cptr->name, cptr->host, c_conf, n_conf));
      return 0;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  attach_conf(cptr, n_conf);
  attach_conf(cptr, c_conf);
  attach_confs(cptr, cptr->name, CONF_HUB | CONF_LEAF);
  /*
   * if the C:line doesn't have an IP address assigned put the one from
   * the client socket there
   */ 
  if (INADDR_NONE == c_conf->ipnum.s_addr)
    c_conf->ipnum.s_addr = cptr->ip.s_addr;

  Debug((DEBUG_DNS,"sv_cl: access ok: [%s]", cptr->host));

  return 1;
}

/*
** send the CAPAB line to a server  -orabidoo
*
* modified, always send all capabilities -Dianora
*/
void send_capabilities(struct Client* cptr, int use_zip)
{
  struct Capability* cap;
  char  msgbuf[BUFSIZE];

  msgbuf[0] = '\0';

  for (cap = captab; cap->name; ++cap)
  {
    /* kludge to rhyme with sludge */

    if (use_zip)
    {
      strcat(msgbuf, cap->name);
      strcat(msgbuf, " ");
    }
    else
    {
      if (cap->cap != CAP_ZIP)
      {
        strcat(msgbuf, cap->name);
        strcat(msgbuf, " ");
      }
    }
  }
  sendto_one(cptr, "CAPAB :%s", msgbuf);
}


static void sendnick_TS(struct Client *cptr, struct Client *acptr)
{
  static char ubuf[12];

  if (IsPerson(acptr))
    {
      send_umode(NULL, acptr, 0, SEND_UMODES, ubuf);
      if (!*ubuf)
        { /* trivial optimization - Dianora */
          
          ubuf[0] = '+';
          ubuf[1] = '\0';
          /*    original was    strcpy(ubuf, "+"); */
        }

      sendto_one(cptr, "NICK %s %d %lu %s %s %s %s :%s", acptr->name, 
                 acptr->hopcount + 1, acptr->tsinfo, ubuf,
                 acptr->username, acptr->host,
                 acptr->user->server, acptr->info);
    }
}


/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an aClient
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */

const char* show_capabilities(struct Client* acptr)
{
  static char        msgbuf[BUFSIZE];
  struct Capability* cap;
  char *t;
  int tl;

  t = msgbuf;
  tl = ircsprintf(msgbuf, "TS ");
  t += tl;

  /* Short circuit if no caps -- send back TS and that's it. */
  if (!acptr->caps)
  {
    msgbuf[2] = '\0';
    return(msgbuf);
  }

  for (cap = captab; cap->cap; ++cap)
  {
    if (cap->cap & acptr->caps)
    {
      tl = ircsprintf(t, "%s ", cap->name);
      t += tl;
    }
  }

  /* Remove the trailing space, and terminate the string. */
  t--;
  *t = '\0';

  return(msgbuf);
}

int server_estab(struct Client *cptr)
{
  struct Channel*   chptr;
  struct Client*    acptr;
  struct ConfItem*  n_conf;
  struct ConfItem*  c_conf;
  const char*       inpath;
  static char       inpath_ip[HOSTLEN * 2 + USERLEN + 5];
  char*             host;
  char*             encr;
  int               split;

  assert(0 != cptr);
  ClearAccess(cptr);

#ifdef HIDE_SERVERS_IPS
  strcpy(inpath_ip, get_client_name(cptr, MASK_IP));
  inpath = get_client_name(cptr, MASK_IP);
#else  
  strcpy(inpath_ip, get_client_name(cptr, SHOW_IP));
  inpath = get_client_name(cptr, HIDE_IP); /* "refresh" inpath with host */
#endif  

  split = irccmp(cptr->name, cptr->host);
  host = cptr->name;

  if (!(n_conf = find_conf_name(cptr->confs, host, CONF_NOCONNECT_SERVER)))
    {
      ServerStats->is_ref++;
      sendto_one(cptr,
                 "ERROR :Access denied. No N line for server %s", inpath_ip);
      sendto_ops("Access denied. No N line for server %s", inpath);
      Log("Access denied. No N line for server %s", inpath_ip);
      return exit_client(cptr, cptr, cptr, "No N line for server");
    }
  if (!(c_conf = find_conf_name(cptr->confs, host, CONF_CONNECT_SERVER )))
    {
      ServerStats->is_ref++;
      sendto_one(cptr, "ERROR :Only N (no C) field for server %s", inpath);
      sendto_realops("Only N (no C) field for server %s",inpath);
      Log("Only N (no C) field for server %s",inpath_ip);
      return exit_client(cptr, cptr, cptr, "No C line for server");
    }
#ifdef CRYPT_LINK_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL. Head it off at the pass... */
  if(*cptr->passwd && *n_conf->passwd)
    {
      extern  char *crypt();
      encr = crypt(cptr->passwd, n_conf->passwd);
    }
  else
    encr = "";
#else
  encr = cptr->passwd;
#endif  /* CRYPT_LINK_PASSWORD */

  if (*n_conf->passwd && 0 != strcmp(n_conf->passwd, encr))
    {
      ServerStats->is_ref++;
      sendto_one(cptr, "ERROR :No Access (passwd mismatch) %s",
		 inpath);
      sendto_realops("Access denied (passwd mismatch) %s", inpath);
      Log("Access denied (passwd mismatch) %s", inpath_ip);
      return exit_client(cptr, cptr, cptr, "Bad Password");
    }
  memset((void *)cptr->passwd, 0,sizeof(cptr->passwd));

  /* Its got identd , since its a server */
  SetGotId(cptr);

#ifndef HUB
  /* Its easy now, if there is a server in my link list
   * and I'm not a HUB, I can't grow the linklist more than 1
   *
   * -Dianora
   */
  if (serv_cptr_list)   
    {
      ServerStats->is_ref++;
      sendto_one(cptr, "ERROR :I'm a leaf not a hub");
      return exit_client(cptr, cptr, cptr, "I'm a leaf");
    }
#endif
  if (IsUnknown(cptr))
    {
	if (c_conf->passwd[0])
         /* sendto_one(cptr,"PASS %s :TS", c_conf->passwd); */
	  sendto_one(cptr,"PASS %s :TS+Ignite", c_conf->passwd); /* To prevent non-Ignite servers from connecting due to protocol mismatch -malign */
	/*
	** Pass my info to the new server
	*/
	
	send_capabilities(cptr,(c_conf->flags & CONF_FLAGS_ZIP_LINK));
	sendto_one(cptr, "SERVER %s 1 :%s",
		   my_name_for_link(me.name, n_conf), 
		   (me.info[0]) ? (me.info) : "IRCers United");
    }
  else
    {
      Debug((DEBUG_INFO, "Check Usernames [%s]vs[%s]",
             n_conf->user, cptr->username));
      if (!match(n_conf->user, cptr->username))
        {
          ServerStats->is_ref++;
          sendto_realops("Username mismatch [%s]v[%s] : %s",
                     n_conf->user, cptr->username,
#ifdef HIDE_SERVERS_IPS
                     get_client_name(cptr, MASK_IP));
#else
                     get_client_name(cptr, TRUE));
#endif
          sendto_one(cptr, "ERROR :No Username Match");
          return exit_client(cptr, cptr, cptr, "Bad User");
        }
    }

#ifdef ZIP_LINKS
  if (IsCapable(cptr, CAP_ZIP) && (c_conf->flags & CONF_FLAGS_ZIP_LINK))
    {
      if (zip_init(cptr) == -1)
        {
          zip_free(cptr);
          sendto_realops("Unable to setup compressed link for %s",
#ifdef HIDE_SERVERS_IPS
                      get_client_name(cptr, MASK_IP));
#else
                      get_client_name(cptr, TRUE));
#endif
          return exit_client(cptr, cptr, &me, "zip_init() failed");
        }
      cptr->flags2 |= (FLAGS2_ZIP|FLAGS2_ZIPFIRST);
    }
  else
    ClearCap(cptr, CAP_ZIP);
#endif /* ZIP_LINKS */

  sendto_one(cptr,"SVINFO %d %d 0 :%lu", TS_CURRENT, TS_MIN, CurrentTime);
  
  det_confs_butmask(cptr, CONF_LEAF|CONF_HUB|CONF_NOCONNECT_SERVER);
  release_client_dns_reply(cptr);
  /*
  ** *WARNING*
  **    In the following code in place of plain server's
  **    name we send what is returned by get_client_name
  **    which may add the "sockhost" after the name. It's
  **    *very* *important* that there is a SPACE between
  **    the name and sockhost (if present). The receiving
  **    server will start the information field from this
  **    first blank and thus puts the sockhost into info.
  **    ...a bit tricky, but you have been warned, besides
  **    code is more neat this way...  --msa
  */
  SetServer(cptr);
  cptr->servptr = &me;
  add_client_to_llist(&(me.serv->servers), cptr);

  Count.server++;
  Count.myserver++;

  /*
   * XXX - this should be in s_bsd
   */
  if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
#ifdef HIDE_SERVERS_IPS
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, MASK_IP), errno);
#else
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);
#endif

  /* LINKLIST */
  /* add to server link list -Dianora */
  cptr->next_server_client = serv_cptr_list;
  serv_cptr_list = cptr;

  fdlist_add(cptr->fd, FDL_SERVER | FDL_BUSY);

  nextping = CurrentTime;
  /* ircd-hybrid-6 can do TS links, and  zipped links*/
  sendto_ops("Link with %s established: (%s) link",
             inpath,show_capabilities(cptr));
  Log("Link with %s established: (%s) link",
             inpath_ip,show_capabilities(cptr));

  add_to_client_hash_table(cptr->name, cptr);
  /* doesnt duplicate cptr->serv if allocated this struct already */
  make_server(cptr);
  cptr->serv->up = me.name;
  /* add it to scache */
  find_or_add(cptr->name);
  
  cptr->serv->nline = n_conf;
  cptr->flags2 |= FLAGS2_CBURST;

  /*
  ** Old sendto_serv_but_one() call removed because we now
  ** need to send different names to different servers
  ** (domain name matching) Send new server to other servers.
  */
  for(acptr=serv_cptr_list;acptr;acptr=acptr->next_server_client)
    {
      if (acptr == cptr)
        continue;

      if ((n_conf = acptr->serv->nline) &&
          match(my_name_for_link(me.name, n_conf), cptr->name))
        continue;

      sendto_one(acptr,":%s SERVER %s 2 :%s",
                 me.name, cptr->name, cptr->info);
    }

  /*
  ** Pass on my client information to the new server
  **
  ** First, pass only servers (idea is that if the link gets
  ** cancelled beacause the server was already there,
  ** there are no NICK's to be cancelled...). Of course,
  ** if cancellation occurs, all this info is sent anyway,
  ** and I guess the link dies when a read is attempted...? --msa
  ** 
  ** Note: Link cancellation to occur at this point means
  ** that at least two servers from my fragment are building
  ** up connection this other fragment at the same time, it's
  ** a race condition, not the normal way of operation...
  **
  ** ALSO NOTE: using the get_client_name for server names--
  **    see previous *WARNING*!!! (Also, original inpath
  **    is destroyed...)
  */

  n_conf = cptr->serv->nline;
  for (acptr = &me; acptr; acptr = acptr->prev)
    {
      /* acptr->from == acptr for acptr == cptr */
      if (acptr->from == cptr)
        continue;
      if (IsServer(acptr))
        {
          if (match(my_name_for_link(me.name, n_conf), acptr->name))
            continue;
          split = (MyConnect(acptr) &&
                   irccmp(acptr->name, acptr->host));

          /* DON'T give away the IP of the server here
           * if its a hub especially.
           */

          if (split)
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->info);
            /*
            sendto_one(cptr, ":%s SERVER %s %d :[%s] %s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->host, acptr->info);
                       */
          else
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1, acptr->info);
        }
    }
  
 
      /*
      ** Send it in the shortened format with the TS, if
      ** it's a TS server; walk the list of channels, sending
      ** all the nicks that haven't been sent yet for each
      ** channel, then send the channel itself -- it's less
      ** obvious than sending all nicks first, but on the
      ** receiving side memory will be allocated more nicely
      ** saving a few seconds in the handling of a split
      ** -orabidoo
      */

  {
    struct SLink* l;
    static char   nickissent = 1;
      
    nickissent = 3 - nickissent;
    /* flag used for each nick to check if we've sent it
       yet - must be different each time and !=0, so we
       alternate between 1 and 2 -orabidoo
       */
    for (chptr = channel; chptr; chptr = chptr->nextch)
      {
        for (l = chptr->members; l; l = l->next)
          {
            acptr = l->value.cptr;
            if (acptr->nicksent != nickissent)
              {
                acptr->nicksent = nickissent;
                if (acptr->from != cptr)
                  sendnick_TS(cptr, acptr);
              }
          }
	send_channel_modes(cptr, chptr);
      }
    /*
    ** also send out those that are not on any channel
    */
    for (acptr = &me; acptr; acptr = acptr->prev)
      if (acptr->nicksent != nickissent)
        {
          acptr->nicksent = nickissent;
          if (acptr->from != cptr)
            sendnick_TS(cptr, acptr);
        }
    }

  cptr->flags2 &= ~FLAGS2_CBURST;

#ifdef  ZIP_LINKS
  /*
  ** some stats about the connect burst,
  ** they are slightly incorrect because of cptr->zip->outbuf.
  */
  if ((cptr->flags2 & FLAGS2_ZIP) && cptr->zip->out->total_in)
    sendto_realops("Connect burst to %s: %lu, compressed: %lu (%3.1f%%)",
#ifdef HIDE_SERVERS_IPS
                get_client_name(cptr, MASK_IP),
#else
                get_client_name(cptr, TRUE),
#endif
                cptr->zip->out->total_in,cptr->zip->out->total_out,
                (100.0*(float)cptr->zip->out->total_out) /
                (float)cptr->zip->out->total_in);
#endif /* ZIP_LINKS */

  /* Always send a PING after connect burst is done */
  sendto_one(cptr, "PING :%s", me.name);

#ifdef NEED_SPLITCODE
#ifdef SPLIT_PONG
  if (server_was_split)
    got_server_pong = NO;
#endif /* SPLIT_PONG */
#endif /* NEED_SPLITCODE */

  return 0;
}

/*
 * set_autoconn - set autoconnect mode
 *
 * inputs       - struct Client pointer to oper requesting change
 *              -
 * output       - none
 * side effects -
 */
void set_autoconn(struct Client *sptr,char *parv0,char *name,int newval)
{
  struct ConfItem *aconf;

  if((aconf= find_conf_by_name(name, CONF_CONNECT_SERVER)))
    {
      if (newval)
        aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
        aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_realops(
                 "%s has changed AUTOCONN for %s to %i",
                 parv0, name, newval);
      sendto_one(sptr,
                 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
                 me.name, parv0, name, newval);
    }
  else
    {
      sendto_one(sptr,
                 ":%s NOTICE %s :Can't find %s",
                 me.name, parv0, name);
    }
}


/*
 * show_servers - send server list to client
 *
 * inputs        - struct Client pointer to client to show server list to
 *               - name of client
 * output        - NONE
 * side effects        -
 */
void show_servers(struct Client *cptr)
{
  struct Client *cptr2;
  int j=0;                /* used to count servers */

  for(cptr2 = serv_cptr_list; cptr2; cptr2 = cptr2->next_server_client)
    {
      ++j;
      sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
                 me.name, RPL_STATSDEBUG, cptr->name, cptr2->name,
                 (cptr2->serv->by[0] ? cptr2->serv->by : "Remote."), 
                 "*", "*", CurrentTime - cptr2->lasttime);

      /*
       * NOTE: moving the username and host to the client struct
       * makes the names in the server->user struct no longer available
       * IMO this is not a big problem because as soon as the user that
       * started the connection leaves the user info has to go away
       * anyhow. Simply showing the nick should be enough here.
       * --Bleep
       */ 
    }

  sendto_one(cptr, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
             cptr->name, j, (j==1) ? "" : "s");
}

