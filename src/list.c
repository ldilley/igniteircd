/************************************************************************
 *   IRC - Internet Relay Chat, src/list.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Finland
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
 *  (C) 1988 University of Oulu, Computing Center and Jarkko Oikarinen
 *
 * $Id: list.c,v 1.2 2006/06/06 04:43:08 malign Exp $
 */
#include "struct.h"
#include "blalloc.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "list.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "res.h"
#include "restart.h"
#include "s_log.h"
#include "send.h"
#include "flud.h"

#include <string.h>
#include <stdlib.h>

/*
 * re-written to use Wohali (joant@cadence.com)
 * block allocator routines. very nicely done Wohali
 *
 * -Dianora
 *
 */

/* Number of Link's to pre-allocate at a time 
   for Efnet 1000 seems reasonable, 
   for smaller nets who knows? -Dianora
   */

#define LINK_PREALLOCATE 32

/* Number of aClient structures to preallocate at a time
   for Efnet 1024 is reasonable 
   for smaller nets who knows? -Dianora
   */

/* This means you call MyMalloc 30 some odd times,
   rather than 30k times -Dianora
*/

#define USERS_PREALLOCATE 32

void    outofmemory(void);

/* for Wohali's block allocator */
BlockHeap *free_Links;
BlockHeap *free_anUsers;
#ifdef FLUD
BlockHeap *free_fludbots;
#endif /* FLUD */

void initlists()
{
  init_client_heap();
  /* Might want to bump up LINK_PREALLOCATE if FLUD is defined */
  free_Links = BlockHeapCreate((size_t)sizeof(Link),LINK_PREALLOCATE);

  /* anUser structs are used by both local aClients, and remote aClients */

  free_anUsers = BlockHeapCreate(sizeof(anUser),
                                 USERS_PREALLOCATE + MAXCONNECTIONS);

#ifdef FLUD
  /* fludbot structs are used to track CTCP Flooders */
  free_fludbots = BlockHeapCreate(sizeof(struct fludbot), MAXCONNECTIONS);
#endif /* FLUD */
}

/*
 * outofmemory()
 *
 * input        - NONE
 * output       - NONE
 * side effects - simply try to report there is a problem
 *                I free all the memory in the kline lists
 *                hoping to free enough memory so that a proper
 *                report can be made. If I was already here (was_here)
 *                then I got called twice, and more drastic measures
 *                are in order. I'll try to just abort() at least.
 *                -Dianora
 */
void outofmemory()
{
  static int was_here = 0;

  if (was_here)
    abort();

  was_here = YES;
  clear_mtrie_conf_links();

  Log("Out of memory: restarting server...");
  restart("Out of Memory");
}

        
/*
** 'make_user' add's an User information block to a client
** if it was not previously allocated.
*/
anUser* make_user(aClient *cptr)
{
  anUser        *user;

  user = cptr->user;
  if (!user)
    {
      user = BlockHeapALLOC(free_anUsers,anUser);
      if( user == (anUser *)NULL)
        outofmemory();
      user->away = NULL;
      user->server = (char *)NULL;      /* scache server name */
      user->refcnt = 1;
      user->joined = 0;
      user->channel = NULL;
      user->invited = NULL;
      cptr->user = user;
    }
  return user;
}


aServer *make_server(aClient *cptr)
{
  aServer* serv = cptr->serv;

  if (!serv)
    {
      serv = (aServer *)MyMalloc(sizeof(aServer));
      memset((void *)serv, 0, sizeof(aServer));

      /* The commented out lines before are
       * for documentation purposes only
       * as they are zeroed by memset above
       */
      /*      serv->user = NULL; */
      /*      serv->users = NULL; */
      /*      serv->servers = NULL; */
      /*      *serv->by = '\0'; */
      /*      serv->up = (char *)NULL; */

      cptr->serv = serv;
    }
  return cptr->serv;
}

/*
** free_user
**      Decrease user reference count by one and release block,
**      if count reaches 0
*/
void _free_user(anUser* user, aClient* cptr)
{
  if (--user->refcnt <= 0)
    {
      if (user->away)
        MyFree((char *)user->away);
      /*
       * sanity check
       */
      if (user->joined || user->refcnt < 0 ||
          user->invited || user->channel)
      sendto_realops("* %#x user (%s!%s@%s) %#x %#x %#x %d %d *",
                 cptr, cptr ? cptr->name : "<noname>",
                 cptr->username, cptr->host, user,
                 user->invited, user->channel, user->joined,
                 user->refcnt);

      if(BlockHeapFree(free_anUsers,user))
        {
          sendto_realops("list.c couldn't BlockHeapFree(free_anUsers,user) user = %lX", user );
          sendto_realops("Please report to the hybrid team! ircd-hybrid@the-project.org");
        }


    }
}

/*
 * Look for ptr in the linked listed pointed to by link.
 */
Link *find_user_link(Link *lp, aClient *ptr)
{
  if (ptr)
    while (lp)
      {
        if (lp->value.cptr == ptr)
          return (lp);
        lp = lp->next;
      }
  return ((Link *)NULL);
}

Link *find_channel_link(Link *lp, aChannel *chptr)
{ 
  if (chptr)
    for(;lp;lp=lp->next)
      if (lp->value.chptr == chptr)
        return lp;
  return ((Link *)NULL);
}

Link *make_link()
{
  Link  *lp;

  lp = BlockHeapALLOC(free_Links,Link);
  if( lp == (Link *)NULL)
    outofmemory();

  lp->next = (Link *)NULL;              /* just to be paranoid... */

  return lp;
}

void _free_link(Link *lp)
{
  if(BlockHeapFree(free_Links,lp))
    {
      sendto_realops("list.c couldn't BlockHeapFree(free_Links,lp) lp = %lX", lp );
      sendto_realops("Please report to the hybrid team!");
    }
}

aClass *make_class()
{
  aClass        *tmp;

  tmp = (aClass *)MyMalloc(sizeof(aClass));
  return tmp;
}

void free_class(aClass *tmp)
{
  MyFree((char *)tmp);
}

/*
Attempt to free up some block memory

list_garbage_collect

inputs          - NONE
output          - NONE
side effects    - memory is possibly freed up
*/

void block_garbage_collect()
{
  BlockHeapGarbageCollect(free_Links);
  BlockHeapGarbageCollect(free_anUsers);
  clean_client_heap();
#ifdef FLUD
  BlockHeapGarbageCollect(free_fludbots);
#endif /* FLUD */
}

/*
 */
void count_user_memory(int *user_memory_used,
                       int *user_memory_allocated )
{
  BlockHeapCountMemory( free_anUsers,
                        user_memory_used,
                        user_memory_allocated);
}

/*
 */
void count_links_memory(int *links_memory_used,
                       int *links_memory_allocated )
{
  BlockHeapCountMemory( free_Links,
                        links_memory_used,
                        links_memory_allocated);
}

#ifdef FLUD
/*
 */
void count_flud_memory(int *flud_memory_used,
                       int *flud_memory_allocated )
{
  BlockHeapCountMemory( free_fludbots,
                        flud_memory_used,
                        flud_memory_allocated);
}
#endif
