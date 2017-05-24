/************************************************************************
 *   IRC - Internet Relay Chat, src/client.c
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
 *  $Id: client.c,v 1.1.1.1 2006/03/08 23:28:08 malign Exp $
 */

#include "client.h"
#include "class.h"
#include "blalloc.h"
#include "channel.h"
#include "common.h"
#include "dline_conf.h"
#include "fdlist.h"
#include "flud.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "m_gline.h"
#include "numeric.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "s_serv.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"
#include "s_debug.h"

#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>


/* 
 * Number of struct Client structures to preallocate at a time
 * for Efnet 1024 is reasonable 
 * for smaller nets who knows? -Dianora
 *
 * This means you call MyMalloc 30 some odd times,
 * rather than 30k times -Dianora
 */
#define CLIENTS_PREALLOCATE 1024

/* 
 * for Wohali's block allocator 
 */
static BlockHeap*        localClientFreeList;
static BlockHeap*        remoteClientFreeList;
static const char* const BH_FREE_ERROR_MESSAGE = \
        "client.c BlockHeapFree failed for cptr = %p";

struct Client* dying_clients[MAXCONNECTIONS]; /* list of dying clients */
char*          dying_clients_reason[MAXCONNECTIONS];

/*
 * init_client_heap - initialize client free memory
 */
void init_client_heap(void)
{
  /* 
   * start off with CLIENTS_PREALLOCATE for now... on typical
   * efnet these days, it can get up to 35k allocated 
   */
  remoteClientFreeList =
    BlockHeapCreate((size_t) CLIENT_REMOTE_SIZE, CLIENTS_PREALLOCATE);
  /* 
   * Can't EVER have more than MAXCONNECTIONS number of local Clients 
   */
  localClientFreeList = 
    BlockHeapCreate((size_t) CLIENT_LOCAL_SIZE, MAXCONNECTIONS);
}

void clean_client_heap(void)
{
  BlockHeapGarbageCollect(localClientFreeList);
  BlockHeapGarbageCollect(remoteClientFreeList);
}

/*
 * make_client - create a new Client struct and set it to initial state.
 *
 *      from == NULL,   create local client (a client connected
 *                      to a socket).
 *
 *      from,   create remote client (behind a socket
 *                      associated with the client defined by
 *                      'from'). ('from' is a local client!!).
 */
struct Client* make_client(struct Client* from)
{
  struct Client* cptr = NULL;

  if (!from)
    {
      cptr = BlockHeapALLOC(localClientFreeList, struct Client);
      if (cptr == NULL)
        outofmemory();
      assert(0 != cptr);

      memset(cptr, 0, CLIENT_LOCAL_SIZE);
      cptr->local_flag = 1;

      cptr->from  = cptr; /* 'from' of local client is self! */
      cptr->since = cptr->lasttime = cptr->firsttime = CurrentTime;

#ifdef NULL_POINTER_NOT_ZERO
#ifdef FLUD
      cptr->fluders   = NULL;
#endif
#ifdef ZIP_LINKS
      cptr->zip       = NULL;
#endif
      cptr->listener  = NULL;
      cptr->confs     = NULL;

      cptr->dns_reply = NULL;
#endif /* NULL_POINTER_NOT_ZERO */
    }
  else
    { /* from is not NULL */
      cptr = BlockHeapALLOC(remoteClientFreeList, struct Client);
      if(cptr == NULL)
        outofmemory();
      assert(0 != cptr);

      memset(cptr, 0, CLIENT_REMOTE_SIZE);
      /* cptr->local_flag = 0; */

      cptr->from = from; /* 'from' of local client is self! */
    }
  SetUnknown(cptr);
  cptr->fd = -1;
  strcpy(cptr->username, "unknown");

#ifdef NULL_POINTER_NOT_ZERO
  /* commenting out unnecessary assigns, but leaving them
   * for documentation. REMEMBER the fripping struct is already
   * zeroed up above =DUH= 
   * -Dianora 
   */
  cptr->listprogress=0;
  cptr->listprogress2=0;
  cptr->next    = NULL;
  cptr->prev    = NULL;
  cptr->hnext   = NULL;
  cptr->idhnext = NULL;
  cptr->lnext   = NULL;
  cptr->lprev   = NULL;
  cptr->next_local_client     = NULL;
  cptr->previous_local_client = NULL;
  cptr->next_server_client    = NULL;
  cptr->next_oper_client      = NULL;
  cptr->user    = NULL;
  cptr->serv    = NULL;
  cptr->servptr = NULL;
  cptr->whowas  = NULL;
#ifdef FLUD
  cptr->fludees = NULL;
#endif
#endif /* NULL_POINTER_NOT_ZERO */

  return cptr;
}

void _free_client(struct Client* cptr)
{
  int result = 0;
  assert(0 != cptr);
  assert(&me != cptr);
  assert(0 == cptr->prev);
  assert(0 == cptr->next);

  if (cptr->local_flag) {
    if (-1 < cptr->fd)
      close(cptr->fd);

    result = BlockHeapFree(localClientFreeList, cptr);
  }
  else
    result = BlockHeapFree(remoteClientFreeList, cptr);

  assert(0 == result);
  if (result)
    {
      /* 
       * Looks "unprofessional" maybe, but I am going to leave this 
       * sendto_ops in it should never happen, and if it does, the 
       * hybrid team wants to hear about it
       */
      sendto_ops(BH_FREE_ERROR_MESSAGE, cptr);
      sendto_ops("Please report to the hybrid team! " \
                 "ircd-hybrid@the-project.org");

      Log(BH_FREE_ERROR_MESSAGE, cptr);
    }
}

/*
 * I re-wrote check_pings a tad
 *
 * check_pings - go through the local client list and check activity
 * kill off stuff that should die
 *
 * inputs       - current time
 * output       - next time_t when check_pings() should be called again
 *
 * side effects - 
 *
 * Clients can be k-lined/d-lined/g-lined/r-lined and exit_client
 * called for each of these.
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 *
 * -Dianora
 */

/* Note, that dying_clients and dying_clients_reason
 * really don't need to be any where near as long as MAXCONNECTIONS
 * but I made it this long for now. If its made shorter,
 * then a limit check is going to have to be added as well
 * -Dianora
 */
time_t check_pings(time_t currenttime)
{               
  struct Client *cptr;          /* current local cptr being examined */
  struct ConfItem     *aconf = (struct ConfItem *)NULL;
  int           ping = 0;               /* ping time value from client */
  int           i;                      /* used to index through fd/cptr's */
  time_t        oldest = 0;             /* next ping time */
  time_t        timeout;                /* found necessary ping time */
  char          *reason;                /* pointer to reason string */
  int           die_index=0;            /* index into list */
  char          ping_time_out_buffer[64];   /* blech that should be a define */

#if defined(IDLE_CHECK) && defined(SEND_FAKE_KILL_TO_CLIENT)
  int           fakekill=0;
#endif /* IDLE_CHECK && SEND_FAKE_KILL_TO_CLIENT */

                                        /* of dying clients */
  dying_clients[0] = (struct Client *)NULL;   /* mark first one empty */

  /*
   * I re-wrote the way klines are handled. Instead of rescanning
   * the local[] array and calling exit_client() right away, I
   * mark the client thats dying by placing a pointer to its struct Client
   * into dying_clients[]. When I have examined all in local[],
   * I then examine the dying_clients[] for struct Client's to exit.
   * This saves the rescan on k-lines, also greatly simplifies the code,
   *
   * Jan 28, 1998
   * -Dianora
   */

   for (i = 0; i <= highest_fd; i++)
    {
      if (!(cptr = local[i]) || IsMe(cptr))
        continue;               /* and go examine next fd/cptr */
      /*
      ** Note: No need to notify opers here. It's
      ** already done when "FLAGS_DEADSOCKET" is set.
      */
      if (cptr->flags & FLAGS_DEADSOCKET)
        {
          /* N.B. EVERY single time dying_clients[] is set
           * it must be followed by an immediate continue,
           * to prevent this cptr from being marked again for exit.
           * If you don't, you could cause exit_client() to be called twice
           * for the same cptr. i.e. bad news
           * -Dianora
           */

          dying_clients[die_index] = cptr;
          dying_clients_reason[die_index++] =
            ((cptr->flags & FLAGS_SENDQEX) ?
             "SendQ exceeded" : "Dead socket");
          dying_clients[die_index] = (struct Client *)NULL;
          continue;             /* and go examine next fd/cptr */
        }

      if (rehashed)
        {
          if(dline_in_progress)
            {
              if( (aconf = match_Dline(ntohl(cptr->ip.s_addr))) )

                  /* if there is a returned 
                   * struct ConfItem then kill it
                   */
                {
                  if(IsConfElined(aconf))
                    {
                      sendto_realops("D-line over-ruled for %s client is E-lined",
                                 get_client_name(cptr,FALSE));
                                 continue;
                      continue;
                    }

                  sendto_realops("D-line active for %s",
                                 get_client_name(cptr, FALSE));

                  dying_clients[die_index] = cptr;
/* Wintrhawk */
#if defined(KLINE_WITH_CONNECTION_CLOSED) && defined(KLINE_WITH_REASON)
                  dying_clients_reason[die_index++] = "Connection closed";
		  reason = aconf->passwd ? aconf->passwd :"D-lined";
#else
#ifdef KLINE_WITH_CONNECTION_CLOSED
                  /*
                   * Use a generic non-descript message here on 
                   * purpose, so as to prevent other users seeing the
                   * client disconnect, from harassing the IRCops.
                   */
                  reason = "Connection closed";
		  dying_clients_reason[die_index++] = reason;
#else
#ifdef KLINE_WITH_REASON
                  reason = aconf->passwd ? aconf->passwd : "D-lined";
		  dying_clients_reason[die_index++] = reason;
#else
                  reason = "D-lined";
		  dying_clients_reason[die_index++] = reason;
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */
#endif /* KLINE_WITH_CONNECTION_CLOSED && KLINE_WITH_REASON */

                  dying_clients[die_index] = (struct Client *)NULL;
                  if(IsPerson(cptr))
                    {
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
                    }
#ifdef REPORT_DLINE_TO_USER
                  else
                    {
                      sendto_one(cptr, "NOTICE DLINE :*** You have been D-lined");
                    }
#endif
                  continue;         /* and go examine next fd/cptr */
                }
            }
          else
            {
              if(IsPerson(cptr))
                {
#ifdef GLINES
                  if( (aconf = find_gkill(cptr,cptr->username)) )
                    {
                      sendto_realops("G-line active for %s",
                                 get_client_name(cptr, FALSE));

                      dying_clients[die_index] = cptr;
/* Wintrhawk */
#if defined(KLINE_WITH_CONNECTION_CLOSED) && defined(KLINE_WITH_REASON)
                     dying_clients_reason[die_index++] = "Connection closed";
		     reason = "Connection closed";
#else
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose, so as to prevent other users seeing the
                       * client, disconnect from harassing the IRCops.
                       */
                      reason = "Connection closed";
		      dying_clients_reason[die_index++] = reason;
#else
#ifdef KLINE_WITH_REASON
                      reason = aconf->passwd ? aconf->passwd : "G-lined";
		      dying_clients_reason[die_index++] = reason;
#else
                      reason = "G-lined";
		      dying_clients_reason[die_index++] = reason;
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */
#endif /* KLINE_WITH_CONNECTION_CLOSED && KLINE_WITH_REASON */

                      dying_clients[die_index] = (struct Client *)NULL;
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);

                      continue;         /* and go examine next fd/cptr */
                    }
                  else
#endif
                  if((aconf = find_kill(cptr))) /* if there is a returned
                                                   struct ConfItem.. then kill it */
                    {
                      if(aconf->status & CONF_ELINE)
                        {
                          sendto_realops("K-line over-ruled for %s client is E-lined",
                                     get_client_name(cptr,FALSE));
                                     continue;
                        }

                      sendto_realops("K-line active for %s",
                                 get_client_name(cptr, FALSE));
                      dying_clients[die_index] = cptr;

/* Wintrhawk */
#if defined(KLINE_WITH_CONNECTION_CLOSED) && defined(KLINE_WITH_REASON)
                      dying_clients_reason[die_index++] = "Connection closed";
		      reason = aconf->passwd ? aconf->passwd :"D-lined";
#else
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose so as to prevent other users seeing the
                       * client disconnect from harassing the IRCops
                       */
                      reason = "Connection closed";
		      dying_clients_reason[die_index++] = reason;
#else
#ifdef KLINE_WITH_REASON
                      reason = aconf->passwd ? aconf->passwd : "K-lined";
		      dying_clients_reason[die_index++] = reason;
#else
                      reason = "K-lined";
		      dying_clients_reason[die_index++] = reason;
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */
#endif /* KLINE_WITH_CONNECTION_CLOSED && KLINE_WITH_REASON */

                      dying_clients[die_index] = (struct Client *)NULL;
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
                      continue;         /* and go examine next fd/cptr */
                    }
                }
            }
        }

#ifdef IDLE_CHECK
      if (IsPerson(cptr))
        {
          if( !IsElined(cptr) &&
              IDLETIME && 
#ifdef OPER_IDLE
              !IsEmpowered(cptr) &&
#endif /* OPER_IDLE */
              !IsIdlelined(cptr) && 
              ((CurrentTime - cptr->user->last) > IDLETIME))
            {
              struct ConfItem *tmpaconf;

              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "Idle time limit exceeded";
#if defined(SEND_FAKE_KILL_TO_CLIENT) && defined(IDLE_CHECK)
              fakekill = 1;
#endif /* SEND_FAKE_KILL_TO_CLIENT && IDLE_CHECK */
              dying_clients[die_index] = (struct Client *)NULL;

              tmpaconf = make_conf();
              tmpaconf->status = CONF_KILL;
              DupString(tmpaconf->host, cptr->host);
              DupString(tmpaconf->passwd, "Idle time limit exceeded" );
              DupString(tmpaconf->name, cptr->username);
              tmpaconf->port = 0;
              tmpaconf->hold = CurrentTime + 60;
              add_temp_kline(tmpaconf);
              sendto_realops("Idle time limit exceeded for %s - temp k-lining",
                         get_client_name(cptr,FALSE));
              continue;         /* and go examine next fd/cptr */
            }
        }
#endif

#ifdef REJECT_HOLD
      if (IsRejectHeld(cptr))
        {
          if( CurrentTime > (cptr->firsttime + REJECT_HOLD_TIME) )
            {
              if( reject_held_fds )
                reject_held_fds--;

              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "reject held client";
              dying_clients[die_index] = (struct Client *)NULL;
              continue;         /* and go examine next fd/cptr */
            }
        }
#endif

      if (!IsRegistered(cptr))
        ping = CONNECTTIMEOUT;
      else
        ping = get_client_ping(cptr);

      /*
       * Ok, so goto's are ugly and can be avoided here but this code
       * is already indented enough so I think its justified. -avalon
       */
       /*  if (!rflag &&
               (ping >= currenttime - cptr->lasttime))
              goto ping_timeout; */

      /*
       * *sigh* I think not -Dianora
       */

      if (ping < (currenttime - cptr->lasttime))
        {

          /*
           * If the server hasnt talked to us in 2*ping seconds
           * and it has a ping time, then close its connection.
           * If the client is a user and a KILL line was found
           * to be active, close this connection too.
           */
          if (((currenttime - cptr->lasttime) >= (2 * ping) &&
               (cptr->flags & FLAGS_PINGSENT)))
            {
              if (IsServer(cptr) || IsConnecting(cptr) ||
                  IsHandshake(cptr))
                {
                  sendto_ops("No response from %s, closing link",
                             get_client_name(cptr, MASK_IP));
                }
              /*
               * this is used for KILL lines with time restrictions
               * on them - send a messgae to the user being killed
               * first.
               * *** Moved up above  -taner ***
               */
              cptr->flags2 |= FLAGS2_PING_TIMEOUT;
              dying_clients[die_index++] = cptr;
              /* the reason is taken care of at exit time */
      /*      dying_clients_reason[die_index++] = "Ping timeout"; */
              dying_clients[die_index] = (struct Client *)NULL;
              
              /*
               * need to start loop over because the close can
               * affect the ordering of the local[] array.- avalon
               *
               ** Not if you do it right - Dianora
               */

              continue;
            }
          else if ((cptr->flags & FLAGS_PINGSENT) == 0)
            {
              /*
               * if we havent PINGed the connection and we havent
               * heard from it in a while, PING it to make sure
               * it is still alive.
               */
              cptr->flags |= FLAGS_PINGSENT;
              /* not nice but does the job */
              cptr->lasttime = currenttime - ping;
              sendto_one(cptr, "PING :%s", me.name);
            }
        }
      /* ping_timeout: */
      timeout = cptr->lasttime + ping;
      while (timeout <= currenttime)
        timeout += ping;
      if (timeout < oldest || !oldest)
        oldest = timeout;

      /*
       * Check UNKNOWN connections - if they have been in this state
       * for > UNKNOWN_TIME, close them.
       */

      if (IsUnknown(cptr))
        {
          if (cptr->firsttime ?
	      ((CurrentTime - cptr->firsttime) > UNKNOWN_TIME) : 0)
            {
              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "Connection Timed Out";
              dying_clients[die_index] = (struct Client *)NULL;
              continue;
            }
        }
    }

  /* Now exit clients marked for exit above.
   * it doesn't matter if local[] gets re-arranged now
   *
   * -Dianora
   */

  for(die_index = 0; (cptr = dying_clients[die_index]); die_index++)
    {
      if(cptr->flags2 & FLAGS2_PING_TIMEOUT)
        {
          (void)ircsprintf(ping_time_out_buffer,
                            "Ping timeout: %d seconds",
                            currenttime - cptr->lasttime);

          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_realops("Client already exited doing ping timeout %X",cptr);
            }
          else
            (void)exit_client(cptr, cptr, &me, ping_time_out_buffer );
          cptr->flags2 |= FLAGS2_ALREADY_EXITED;
        }
      else
#if defined(SEND_FAKE_KILL_TO_CLIENT) && defined(IDLE_CHECK)
        {
	  char *killer;
	  char *p;

	  killer = "AutoKILL";
          if (fakekill)
            sendto_prefix_one(cptr, cptr, ":%s KILL %s :(%s)", killer,
            cptr->name, dying_clients_reason[die_index]);
	  (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
        }
#else 
      {
	(void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
      }

#endif /* SEND_FAKE_KILL_TO_CLIENT && IDLE_CHECK */
    }

  rehashed = 0;
  dline_in_progress = 0;

  if (!oldest || oldest < currenttime)
    oldest = currenttime + PINGFREQUENCY;
  Debug((DEBUG_NOTICE, "Next check_ping() call at: %s, %d %d %d",
         myctime(oldest), ping, oldest, currenttime));
  
  return (oldest);
}

static void update_client_exit_stats(struct Client* cptr)
{
  if (IsServer(cptr))
    {
      --Count.server;

      sendto_realops_flags(FLAGS_EXTERNAL, "Server %s split from %s.",
                           cptr->name, cptr->servptr->name);

#ifdef NEED_SPLITCODE
      /* Don't bother checking for a split, if split code
       * is deactivated with server_split_recovery_time == 0
       */
      if(SPLITDELAY && (Count.server < SPLITNUM))
        {
          if (!server_was_split)
            {
              sendto_ops("Netsplit detected, split-mode activated.");
              server_was_split = YES;
            }
          server_split_time = CurrentTime;
        }
#endif
    }

  else if (IsClient(cptr)) {
    --Count.total;
    if (IsEmpowered(cptr))
      --Count.oper;
    if (IsInvisible(cptr)) 
      --Count.invisi;
  }
}

static void release_client_state(struct Client* cptr)
{
  if (cptr->user) {
    if (IsPerson(cptr)) {
      add_history(cptr,0);
      off_history(cptr);
    }
    free_user(cptr->user, cptr); /* try this here */
  }
  if (cptr->serv)
    {
      if (cptr->serv->user)
        free_user(cptr->serv->user, cptr);
      MyFree((char*) cptr->serv);
    }

#ifdef FLUD
  if (MyConnect(cptr))
    free_fluders(cptr, NULL);
  free_fludees(cptr);
#endif
}

/*
 * taken the code from ExitOneClient() for this and placed it here.
 * - avalon
 */
void remove_client_from_list(struct Client* cptr)
{
  assert(0 != cptr);

 /* HACK somehow this client has already exited
  * but has come back to haunt us.. looks like a bug
  */
  if(!cptr->prev && !cptr->next)
    {
      Log("already exited client %X [%s]",
        cptr,
        cptr->name?cptr->name:"NULL" );
      return;
    }

  if (cptr->prev)
    cptr->prev->next = cptr->next;
  else
    {
      GlobalClientList = cptr->next;
      GlobalClientList->prev = NULL;
    }
  if (cptr->next)
    cptr->next->prev = cptr->prev;
  cptr->next = cptr->prev = NULL;

  /*
   * XXX - this code should be elsewhere
   */
  update_client_exit_stats(cptr);
  release_client_state(cptr);
  free_client(cptr);
}

/*
 * although only a small routine, it appears in a number of places
 * as a collection of a few lines...functions like this *should* be
 * in this file, shouldnt they ?  after all, this is list.c, isnt it ?
 * -avalon
 */
void add_client_to_list(struct Client *cptr)
{
  /*
   * since we always insert new clients to the top of the list,
   * this should mean the "me" is the bottom most item in the list.
   */
  cptr->next = GlobalClientList;
  GlobalClientList = cptr;
  if (cptr->next)
    cptr->next->prev = cptr;
  return;
}

/* Functions taken from +CSr31, paranoified to check that the client
** isn't on a llist already when adding, and is there when removing -orabidoo
*/
void add_client_to_llist(struct Client **bucket, struct Client *client)
{
  if (!client->lprev && !client->lnext)
    {
      client->lprev = NULL;
      if ((client->lnext = *bucket) != NULL)
        client->lnext->lprev = client;
      *bucket = client;
    }
}

void del_client_from_llist(struct Client **bucket, struct Client *client)
{
  if (client->lprev)
    {
      client->lprev->lnext = client->lnext;
    }
  else if (*bucket == client)
    {
      *bucket = client->lnext;
    }
  if (client->lnext)
    {
      client->lnext->lprev = client->lprev;
    }
  client->lnext = client->lprev = NULL;
}

/*
 *  find_client - find a client (server or user) by name.
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string and the search is the for server and user.
 */
struct Client* find_client(const char* name, struct Client *cptr)
{
  if (name)
    cptr = hash_find_client(name, cptr);

  return cptr;
}

/*
 *  find_userhost - find a user@host (server or user).
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string and the search is the for server and user.
 */
struct Client *find_userhost(char *user, char *host, struct Client *cptr, int *count)
{
  struct Client       *c2ptr;
  struct Client       *res = cptr;

  *count = 0;
  if (collapse(user))
    for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next) 
      {
        if (!MyClient(c2ptr)) /* implies mine and a user */
          continue;
        if ((!host || match(host, c2ptr->host)) &&
            irccmp(user, c2ptr->username) == 0)
          {
            (*count)++;
            res = c2ptr;
          }
      }
  return res;
}

/*
 *  find_server - find server by name.
 *
 *      This implementation assumes that server and user names
 *      are unique, no user can have a server name and vice versa.
 *      One should maintain separate lists for users and servers,
 *      if this restriction is removed.
 *
 *  *Note*
 *      Semantics of this function has been changed from
 *      the old. 'name' is now assumed to be a null terminated
 *      string.
 */
struct Client* find_server(const char* name)
{
  if (name)
    return hash_find_server(name);
  return 0;
}

/*
 * next_client - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client*
next_client(struct Client *next,     /* First client to check */
            const char* ch)          /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return ((struct Client *) NULL);

  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name)) break;
    }
  return next;
}


/* 
 * this slow version needs to be used for hostmasks *sigh
 *
 * next_client_double - find the next matching client. 
 * The search can be continued from the specified client entry. 
 * Normal usage loop is:
 *
 *      for (x = client; x = next_client(x,mask); x = x->next)
 *              HandleMatchingClient;
 *            
 */
struct Client* 
next_client_double(struct Client *next, /* First client to check */
                   const char* ch)      /* search string (may include wilds) */
{
  struct Client *tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return NULL;
  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (match(ch,next->name) || match(next->name,ch))
        break;
    }
  return next;
}

#if 0
/*
 * find_server_by_name - attempt to find server in hash table, otherwise 
 * scan the GlobalClientList
 */
struct Client* find_server_by_name(const char* name)
{
  struct Client* cptr = 0;

  if (EmptyString(name))
    return cptr;

  if ((cptr = hash_find_server(name)))
    return cptr;
  /*
   * XXX - this shouldn't be needed at all hash_find_server should
   * find hostmasked names
   */
  if (!strchr(name, '*'))
    return cptr;

  /* hmmm hot spot for host masked servers (ick)
   * a separate link list for all servers would help here
   * instead of having to scan 50k client structs. (ick)
   * -Dianora
   */
  for (cptr = GlobalClientList; cptr; cptr = cptr->next)
    {
      if (!IsServer(cptr) && !IsMe(cptr))
        continue;
      if (match(name, cptr->name))
        break;
      if (strchr(cptr->name, '*'))
        if (match(cptr->name, name))
          break;
    }
  return cptr;
}
#endif

/*
 *  find_person - find person by (nick)name.
 */
struct Client *find_person(char *name, struct Client *cptr)
{
  struct Client       *c2ptr = cptr;

  c2ptr = find_client(name, c2ptr);

  if (c2ptr && IsClient(c2ptr) && c2ptr->user)
    return c2ptr;
  else
    return cptr;
}

/*
 * find_chasing - find the client structure for a nick name (user) 
 *      using history mechanism if necessary. If the client is not found, 
 *      an error message (NO SUCH NICK) is generated. If the client was found
 *      through the history, chasing will be 1 and otherwise 0.
 */
struct Client *find_chasing(struct Client *sptr, char *user, int *chasing)
{
  struct Client *who = find_client(user, (struct Client *)NULL);
  
  if (chasing)
    *chasing = 0;
  if (who)
    return who;
  if (!(who = get_history(user, (long)KILLCHASETIMELIMIT)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                 me.name, sptr->name, user);
      return ((struct Client *)NULL);
    }
  if (chasing)
    *chasing = 1;
  return who;
}



/*
 * check_registered_user - is used to cancel message, if the
 * originator is a server or not registered yet. In other
 * words, passing this test, *MUST* guarantee that the
 * sptr->user exists (not checked after this--let there
 * be coredumps to catch bugs... this is intentional --msa ;)
 *
 * There is this nagging feeling... should this NOT_REGISTERED
 * error really be sent to remote users? This happening means
 * that remote servers have this user registered, although this
 * one has it not... Not really users fault... Perhaps this
 * error message should be restricted to local clients and some
 * other thing generated for remotes...
 */
int check_registered_user(struct Client* client)
{
  if (!IsRegisteredUser(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * check_registered user cancels message, if 'x' is not
 * registered (e.g. we don't know yet whether a server
 * or user)
 */
int check_registered(struct Client* client)
{
  if (!IsRegistered(client))
    {
      sendto_one(client, form_str(ERR_NOTREGISTERED), me.name, "*");
      return -1;
    }
  return 0;
}

/*
 * release_client_dns_reply - remove client dns_reply references
 *
 */
void release_client_dns_reply(struct Client* client)
{
  assert(0 != client);
#if 0
  if (client->dns_reply)
    {
      --client->dns_reply->ref_count;
      client->dns_reply = 0;
    }
#endif
}

/*
 * get_client_name -  Return the name of the client
 *    for various tracking and
 *      admin purposes. The main purpose of this function is to
 *      return the "socket host" name of the client, if that
 *        differs from the advertised name (other than case).
 *        But, this can be used to any client structure.
 *
 * NOTE 1:
 *        Watch out the allocation of "nbuf", if either sptr->name
 *        or sptr->sockhost gets changed into pointers instead of
 *        directly allocated within the structure...
 *
 * NOTE 2:
 *        Function return either a pointer to the structure (sptr) or
 *        to internal buffer (nbuf). *NEVER* use the returned pointer
 *        to modify what it points!!!
 */
/* Apparently, the use of (+) for idented clients
 * is unstandard. As it is a pain to parse, I'm just as happy
 * to remove it. It also simplifies the code a bit. -Dianora
 */

const char* get_client_name(struct Client* client, int showip)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];

  assert(0 != client);

  if (MyConnect(client))
    {
      if (!irccmp(client->name, client->host))
        return client->name;

#ifdef HIDE_SERVERS_IPS
      if(IsServer(client) || IsHandshake(client) || IsConnecting(client))
      {
        ircsprintf(nbuf, "%s[%s@255.255.255.255]", client->name,
	           client->username);
        return nbuf;
      }
#endif

      /* And finally, let's get the host information, ip or name */
      switch (showip)
        {
          case SHOW_IP:
#ifndef SERVERHIDE
            ircsprintf(nbuf, "%s[%s@%s]", client->name, client->username,
              client->sockhost);
            break;
#endif
          case MASK_IP:
            ircsprintf(nbuf, "%s[%s@255.255.255.255]", client->name,
              client->username);
            break;
          default:
            ircsprintf(nbuf, "%s[%s@%s]", client->name, client->username,
              client->host);
        }
      return nbuf;
    }

  /* As pointed out by Adel Mezibra 
   * Neph|l|m@EFnet. Was missing a return here.
   */
  return client->name;
}

const char* get_client_host(struct Client* client)
{
  static char nbuf[HOSTLEN * 2 + USERLEN + 5];
  
  assert(0 != client);

  if (!MyConnect(client))
    return client->name;
  if (!client->host)
    return get_client_name(client, FALSE);
  else
    {
      ircsprintf(nbuf, "%s[%-.*s@%-.*s]",
                 client->name, USERLEN, client->username,
                 HOSTLEN, client->host);
    }
  return nbuf;
}

/*
** Exit one client, local or remote. Assuming all dependents have
** been already removed, and socket closed for local client.
*/
static void exit_one_client(struct Client *cptr, struct Client *sptr, struct Client *from,
                            const char* comment)
{
  struct Client* acptr;
  Link*    lp;

  if (IsServer(sptr))
    {
      if (sptr->servptr && sptr->servptr->serv)
        del_client_from_llist(&(sptr->servptr->serv->servers),
                                    sptr);
      else
        ts_warn("server %s without servptr!", sptr->name);
    }
  else if (sptr->servptr && sptr->servptr->serv)
      del_client_from_llist(&(sptr->servptr->serv->users), sptr);
  /* there are clients w/o a servptr: unregistered ones */

  /*
  **  For a server or user quitting, propogate the information to
  **  other servers (except to the one where is came from (cptr))
  */
  if (IsMe(sptr))
    {
      sendto_ops("ERROR: tried to exit me! : %s", comment);
      return;        /* ...must *never* exit self!! */
    }
  else if (IsServer(sptr))
    {
      /*
      ** Old sendto_serv_but_one() call removed because we now
      ** need to send different names to different servers
      ** (domain name matching)
      */
      /*
      ** The bulk of this is done in remove_dependents now, all
      ** we have left to do is send the SQUIT upstream.  -orabidoo
      */
      acptr = sptr->from;
      if (acptr && IsServer(acptr) && acptr != cptr && !IsMe(acptr) &&
          (sptr->flags & FLAGS_KILLED) == 0)
        sendto_one(acptr, ":%s SQUIT %s :%s", from->name, sptr->name, comment);
    }
  else if (!(IsPerson(sptr)))
      /* ...this test is *dubious*, would need
      ** some thought.. but for now it plugs a
      ** nasty hole in the server... --msa
      */
      ; /* Nothing */
  else if (sptr->name[0]) /* ...just clean all others with QUIT... */
    {
      /*
      ** If this exit is generated from "m_kill", then there
      ** is no sense in sending the QUIT--KILL's have been
      ** sent instead.
      */
      if ((sptr->flags & FLAGS_KILLED) == 0)
        {
          sendto_serv_butone(cptr,":%s QUIT :%s",
                             sptr->name, comment);
        }
      /*
      ** If a person is on a channel, send a QUIT notice
      ** to every client (person) on the same channel (so
      ** that the client can show the "**signoff" message).
      ** (Note: The notice is to the local clients *only*)
      */
      if (sptr->user)
        {
          sendto_common_channels(sptr, ":%s QUIT :%s",
                                   sptr->name, comment);

          while ((lp = sptr->user->channel))
            remove_user_from_channel(sptr,lp->value.chptr,0);
          
          /* Clean up invitefield */
          while ((lp = sptr->user->invited))
            del_invite(sptr, lp->value.chptr);
          /* again, this is all that is needed */
        }
    }
  
  /* 
   * Remove sptr from the client lists
   */
  del_from_client_hash_table(sptr->name, sptr);
  remove_client_from_list(sptr);
}

/*
** Recursively send QUITs and SQUITs for sptr and all its dependent clients
** and servers to those servers that need them.  A server needs the client
** QUITs if it can't figure them out from the SQUIT (ie pre-TS4) or if it
** isn't getting the SQUIT because of @#(*&@)# hostmasking.  With TS4, once
** a link gets a SQUIT, it doesn't need any QUIT/SQUITs for clients depending
** on that one -orabidoo
*/
static void recurse_send_quits(struct Client *cptr, struct Client *sptr, struct Client *to,
                                const char* comment,  /* for servers */
                                const char* myname)
{
  struct Client *acptr;

  /* If this server can handle quit storm (QS) removal
   * of dependents, just send the SQUIT -Dianora
   */

  if (IsCapable(to,CAP_QS))
    {
      if (match(myname, sptr->name))
        {
          for (acptr = sptr->serv->users; acptr; acptr = acptr->lnext)
            sendto_one(to, ":%s QUIT :%s", acptr->name, comment);
          for (acptr = sptr->serv->servers; acptr; acptr = acptr->lnext)
            recurse_send_quits(cptr, acptr, to, comment, myname);
        }
      else
        sendto_one(to, "SQUIT %s :%s", sptr->name, me.name);
    }
  else
    {
      for (acptr = sptr->serv->users; acptr; acptr = acptr->lnext)
        sendto_one(to, ":%s QUIT :%s", acptr->name, comment);
      for (acptr = sptr->serv->servers; acptr; acptr = acptr->lnext)
        recurse_send_quits(cptr, acptr, to, comment, myname);
      if (!match(myname, sptr->name))
        sendto_one(to, "SQUIT %s :%s", sptr->name, me.name);
    }
}

/* 
** Remove all clients that depend on sptr; assumes all (S)QUITs have
** already been sent.  we make sure to exit a server's dependent clients 
** and servers before the server itself; exit_one_client takes care of 
** actually removing things off llists.   tweaked from +CSr31  -orabidoo
*/
/*
 * added sanity test code.... sptr->serv might be NULL... -Dianora
 */
static void recurse_remove_clients(struct Client* sptr, const char* comment)
{
  struct Client *acptr;

  if (IsMe(sptr))
    return;

  if (!sptr->serv)        /* oooops. uh this is actually a major bug */
    return;

  while ( (acptr = sptr->serv->servers) )
    {
      recurse_remove_clients(acptr, comment);
      /*
      ** a server marked as "KILLED" won't send a SQUIT 
      ** in exit_one_client()   -orabidoo
      */
      acptr->flags |= FLAGS_KILLED;
      exit_one_client(NULL, acptr, &me, me.name);
    }

  while ( (acptr = sptr->serv->users) )
    {
      acptr->flags |= FLAGS_KILLED;
      exit_one_client(NULL, acptr, &me, comment);
    }
}

/*
** Remove *everything* that depends on sptr, from all lists, and sending
** all necessary QUITs and SQUITs.  sptr itself is still on the lists,
** and its SQUITs have been sent except for the upstream one  -orabidoo
*/
static void remove_dependents(struct Client* cptr, 
                               struct Client* sptr,
                               struct Client* from,
                               const char* comment,
                               const char* comment1)
{
  struct Client *to;
  int i;
  struct ConfItem *aconf;
  static char myname[HOSTLEN+1];

  for (i=0; i<=highest_fd; i++)
    {
      if (!(to = local[i]) || !IsServer(to) || IsMe(to) ||
          to == sptr->from || (to == cptr && IsCapable(to,CAP_QS)))
        continue;
      /* MyConnect(sptr) is rotten at this point: if sptr
       * was mine, ->from is NULL.  we need to send a 
       * WALLOPS here only if we're "deflecting" a SQUIT
       * that hasn't hit its target  -orabidoo
       */
      /* The WALLOPS isn't needed here as pointed out by
       * comstud, since m_squit already does the notification.
       */
#if 0
      if (to != cptr &&        /* not to the originator */
          to != sptr->from && /* not to the destination */
          cptr != sptr->from        /* hasn't reached target */
          && sptr->servptr != &me) /* not mine [done in m_squit] */
        sendto_one(to, ":%s WALLOPS :Received SQUIT %s from %s (%s)",
                   me.name, sptr->name, get_client_name(from, FALSE), comment);

#endif
      if ((aconf = to->serv->nline))
        strncpy_irc(myname, my_name_for_link(me.name, aconf), HOSTLEN);
      else
        strncpy_irc(myname, me.name, HOSTLEN);
      recurse_send_quits(cptr, sptr, to, comment1, myname);
    }

  recurse_remove_clients(sptr, comment1);
}


/*
** exit_client - This is old "m_bye". Name  changed, because this is not a
**        protocol function, but a general server utility function.
**
**        This function exits a client of *any* type (user, server, etc)
**        from this server. Also, this generates all necessary prototol
**        messages that this exit may cause.
**
**   1) If the client is a local client, then this implicitly
**        exits all other clients depending on this connection (e.g.
**        remote clients having 'from'-field that points to this.
**
**   2) If the client is a remote client, then only this is exited.
**
** For convenience, this function returns a suitable value for
** m_function return value:
**
**        CLIENT_EXITED        if (cptr == sptr)
**        0                if (cptr != sptr)
*/
int exit_client(
struct Client* cptr,        /*
                ** The local client originating the exit or NULL, if this
                ** exit is generated by this server for internal reasons.
                ** This will not get any of the generated messages.

                */
struct Client* sptr,        /* Client exiting */
struct Client* from,        /* Client firing off this Exit, never NULL! */
const char* comment        /* Reason for the exit */
                   )
{
  struct Client        *acptr;
  struct Client        *next;
  char comment1[HOSTLEN + HOSTLEN + 2];
  if (MyConnect(sptr))
    {
#ifdef LIMIT_UH
      if(sptr->flags & FLAGS_IPHASH)
        remove_one_ip(sptr);
#else
      if(sptr->flags & FLAGS_IPHASH)
        remove_one_ip(sptr->ip.s_addr);
#endif
      if (IsEmpowered(sptr))
        {
          fdlist_delete(sptr->fd, FDL_OPER | FDL_BUSY);
          /* LINKLIST */
          /* oh for in-line functions... */
          {
            struct Client *prev_cptr=(struct Client *)NULL;
            struct Client *cur_cptr = oper_cptr_list;
            while(cur_cptr) 
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_oper_client = cur_cptr->next_oper_client;
                    else
                      oper_cptr_list = cur_cptr->next_oper_client;
                    cur_cptr->next_oper_client = (struct Client *)NULL;
                    break;
                  }
                else
                  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_oper_client;
              }
          }
        }
      if (IsClient(sptr))
        {
          Count.local--;

          /* LINKLIST */
          /* oh for in-line functions... */
          if(IsPerson(sptr))        /* a little extra paranoia */
            {
              if(sptr->previous_local_client)
                sptr->previous_local_client->next_local_client =
                  sptr->next_local_client;
              else
                {
                  if(local_cptr_list == sptr)
                    {
                      local_cptr_list = sptr->next_local_client;
                    }
                }

              if(sptr->next_local_client)
                sptr->next_local_client->previous_local_client =
                  sptr->previous_local_client;

              sptr->previous_local_client = sptr->next_local_client = 
                (struct Client *)NULL;
            }
        }
      if (IsServer(sptr))
        {
          Count.myserver--;
          fdlist_delete(sptr->fd, FDL_SERVER | FDL_BUSY);

          /* LINKLIST */
          /* oh for in-line functions... */
          {
            struct Client *prev_cptr = NULL;
            struct Client *cur_cptr = serv_cptr_list;
            while(cur_cptr)
              {
                if(sptr == cur_cptr)
                  {
                    if(prev_cptr)
                      prev_cptr->next_server_client =
                        cur_cptr->next_server_client;
                    else
                      serv_cptr_list = cur_cptr->next_server_client;
                    cur_cptr->next_server_client = NULL;
                    break;
                  }
                else
                  prev_cptr = cur_cptr;
                cur_cptr = cur_cptr->next_server_client;
              }
          }
        }
      sptr->flags |= FLAGS_CLOSING;
      if (IsPerson(sptr))
        {
          sendto_realops_flags(FLAGS_CCONN,
                               "Client exiting: %s (%s@%s) [%s] [%s]",
                               sptr->name, sptr->username, sptr->host,
#ifdef WINTRHAWK
                               comment,
#else
               (sptr->flags & FLAGS_NORMALEX) ?  "Client Quit" : comment,
#endif /* WINTRHAWK */
#ifdef HIDE_SPOOF_IPS
                               IsIPSpoof(sptr) ? "255.255.255.255" :
#endif /* HIDE_SPOOF_IPS */
                               sptr->sockhost);
        }
          if (sptr->fd >= 0)
            {
              if (IsRegistered(sptr) && !IsServer(sptr))
                { /* jeremy is anal retentive */
                  sendto_one(sptr, "ERROR :Closing Link: %s (%s)",
                             get_client_name(sptr, SHOW_IP), comment);
                } 
              else
                {
                  sendto_one(sptr, "ERROR :Closing Link: %s (%s)",
                             get_client_name(sptr, MASK_IP), comment);
                }
            }
          /*
          ** Currently only server connections can have
          ** depending remote clients here, but it does no
          ** harm to check for all local clients. In
          ** future some other clients than servers might
          ** have remotes too...
          **
          ** Close the Client connection first and mark it
          ** so that no messages are attempted to send to it.
          ** (The following *must* make MyConnect(sptr) == FALSE!).
          ** It also makes sptr->from == NULL, thus it's unnecessary
          ** to test whether "sptr != acptr" in the following loops.
          */

          close_connection(sptr);
    }

  if(IsServer(sptr))
    {        
#ifdef SERVERHIDE
      strcpy(comment1, me.name);
      strcat(comment1, " ");
      strcat(comment1, me.name);
#else 
      /* I'm paranoid -Dianora */
      if((sptr->serv) && (sptr->serv->up))
        strcpy(comment1, sptr->serv->up);
      else
        strcpy(comment1, "<Unknown>" );

      strcat(comment1," ");
      strcat(comment1, sptr->name);
#endif

      remove_dependents(cptr, sptr, from, comment, comment1);

      if (sptr->servptr == &me)
        {
          sendto_ops("%s was connected for %d seconds.  %d/%d sendK/recvK.",
                     sptr->name, CurrentTime - sptr->firsttime,
                     sptr->sendK, sptr->receiveK);
          Log("%s was connected for %d seconds.  %d/%d sendK/recvK.",
              sptr->name, CurrentTime - sptr->firsttime, 
              sptr->sendK, sptr->receiveK);

              /* Just for paranoia... this shouldn't be necessary if the
              ** remove_dependents() stuff works, but it's still good
              ** to do it.    MyConnect(sptr) has been set to false,
              ** so we look at servptr, which should be ok  -orabidoo
              */
              for (acptr = GlobalClientList; acptr; acptr = next)
                {
                  next = acptr->next;
                  if (!IsServer(acptr) && acptr->from == sptr)
                    {
                      ts_warn("Dependent client %s not on llist!?",
                              acptr->name);
                      exit_one_client(NULL, acptr, &me, comment1);
                    }
                }
              /*
              ** Second SQUIT all servers behind this link
              */
              for (acptr = GlobalClientList; acptr; acptr = next)
                {
                  next = acptr->next;
                  if (IsServer(acptr) && acptr->from == sptr)
                    {
                      ts_warn("Dependent server %s not on llist!?", 
                                     acptr->name);
                      exit_one_client(NULL, acptr, &me, me.name);
                    }
                }
            }
        }

  exit_one_client(cptr, sptr, from, comment);
  return cptr == sptr ? CLIENT_EXITED : 0;
}

/*
 * Count up local client memory
 */
void count_local_client_memory(int *local_client_memory_used,
                               int *local_client_memory_allocated )
{
  BlockHeapCountMemory( localClientFreeList,
                        local_client_memory_used,
                        local_client_memory_allocated);
}

/*
 * Count up remote client memory
 */
void count_remote_client_memory(int *remote_client_memory_used,
                               int *remote_client_memory_allocated )
{
  BlockHeapCountMemory( remoteClientFreeList,
                        remote_client_memory_used,
                        remote_client_memory_allocated);
}

