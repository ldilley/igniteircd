/************************************************************************
*   IRC - Internet Relay Chat, src/whowas.c
*   Copyright (C) 1990 Markku Savela
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
*   $Id: whowas.c,v 1.1.1.1 2006/03/08 23:28:14 malign Exp $
*/
#include "whowas.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "struct.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>


/* internally defined function */
static void add_whowas_to_clist(aWhowas **,aWhowas *);
static void del_whowas_from_clist(aWhowas **,aWhowas *);
static void add_whowas_to_list(aWhowas **,aWhowas *);
static void del_whowas_from_list(aWhowas **,aWhowas *);

static aWhowas WHOWAS[NICKNAMEHISTORYLENGTH];
static aWhowas *WHOWASHASH[WW_MAX];

static int whowas_next = 0;

static unsigned int hash_whowas_name(const char* name)
{
  unsigned int h = 0;

  while (*name)
    {
      h = (h << 4) - (h + (unsigned char)ToLower(*name++));
    }
  return(h & (WW_MAX - 1));
}

void add_history(aClient* cptr, int online)
{
  aWhowas* who = &WHOWAS[whowas_next];

  assert(0 != cptr);

  if (who->hashv != -1)
    {
      if (who->online)
        del_whowas_from_clist(&(who->online->whowas),who);
      del_whowas_from_list(&WHOWASHASH[who->hashv], who);
    }
  who->hashv = hash_whowas_name(cptr->name);
  who->logoff = CurrentTime;
  /*
   * NOTE: strcpy ok here, the sizes in the client struct MUST
   * match the sizes in the whowas struct
   */
  strncpy_irc(who->name, cptr->name, NICKLEN);
  who->name[NICKLEN] = '\0';
  strcpy(who->username, cptr->username);
  strcpy(who->hostname, cptr->host);
  strcpy(who->realname, cptr->info);

  /* Its not string copied, a pointer to the scache hash is copied
     -Dianora
   */
  /*  strncpy_irc(who->servername, cptr->user->server, HOSTLEN); */
  who->servername = cptr->user->server;

  if (online)
    {
      who->online = cptr;
      add_whowas_to_clist(&(cptr->whowas), who);
    }
  else
    who->online = NULL;
  add_whowas_to_list(&WHOWASHASH[who->hashv], who);
  whowas_next++;
  if (whowas_next == NICKNAMEHISTORYLENGTH)
    whowas_next = 0;
}

void off_history(aClient *cptr)
{
  aWhowas *temp, *next;

  for(temp=cptr->whowas;temp;temp=next)
    {
      next = temp->cnext;
      temp->online = NULL;
      del_whowas_from_clist(&(cptr->whowas), temp);
    }
}

aClient *get_history(char *nick,time_t timelimit)
{
  aWhowas *temp;
  int blah;

  timelimit = CurrentTime - timelimit;
  blah = hash_whowas_name(nick);
  temp = WHOWASHASH[blah];
  for(;temp;temp=temp->next)
    {
      if (irccmp(nick, temp->name))
        continue;
      if (temp->logoff < timelimit)
        continue;
      return temp->online;
    }
  return NULL;
}

void    count_whowas_memory(int *wwu,
                            u_long *wwum)
{
  aWhowas *tmp;
  int i;
  int     u = 0;
  u_long  um = 0;

  /* count the number of used whowas structs in 'u' */
  /* count up the memory used of whowas structs in um */

  for (i = 0, tmp = &WHOWAS[0]; i < NICKNAMEHISTORYLENGTH; i++, tmp++)
    if (tmp->hashv != -1)
      {
        u++;
        um += sizeof(aWhowas);
      }
  *wwu = u;
  *wwum = um;
  return;
}
/*
** m_whowas
**      parv[0] = sender prefix
**      parv[1] = nickname queried
*/
int     m_whowas(aClient *cptr,
                 aClient *sptr,
                 int parc,
                 char *parv[])
{
  aWhowas *temp;
  int cur = 0;
  int     max = -1, found = 0;
  char    *p, *nick;
/*  char    *s; */
  static time_t last_used=0L;

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NONICKNAMEGIVEN),
                 me.name, parv[0]);
      return 0;
    }
  if (parc > 2)
    max = atoi(parv[2]);
  if (parc > 3)
    if (hunt_server(cptr,sptr,":%s WHOWAS %s %s :%s", 3,parc,parv))
      return 0;

  if(!IsEmpowered(sptr) && !MyConnect(sptr)) /* pace non local requests */
    {
      if((last_used + WHOIS_WAIT) > CurrentTime)
        {
          return 0;
        }
      else
        {
          last_used = CurrentTime;
        }
    }

  if (!MyConnect(sptr) && (max > 20))
    max = 20;
  /*  for (s = parv[1]; (nick = strtoken(&p, s, ",")); s = NULL) */
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  nick = parv[1];

  temp = WHOWASHASH[hash_whowas_name(nick)];
  found = 0;
  for(;temp;temp=temp->next)
    {
      if (!irccmp(nick, temp->name))
        {
          sendto_one(sptr, form_str(RPL_WHOWASUSER),
                     me.name, parv[0], temp->name,
                     temp->username,
                     temp->hostname,
                     temp->realname);
          sendto_one(sptr, form_str(RPL_WHOISSERVER),
                     me.name,
                     parv[0],
                     temp->name,
#ifdef SERVERHIDE
                     IsEmpowered(sptr) ? temp->servername : NETWORK_NAME,
#else
                     temp->servername,
#endif
                     myctime(temp->logoff));
          cur++;
          found++;
        }
      if (max > 0 && cur >= max)
        break;
    }
  if (!found)
    sendto_one(sptr, form_str(ERR_WASNOSUCHNICK),
               me.name, parv[0], nick);

  sendto_one(sptr, form_str(RPL_ENDOFWHOWAS), me.name, parv[0], parv[1]);
  return 0;
}

void    initwhowas()
{
  int i;

  for (i=0;i<NICKNAMEHISTORYLENGTH;i++)
    {
      memset((void *)&WHOWAS[i], 0, sizeof(aWhowas));
      WHOWAS[i].hashv = -1;
    }
  for (i=0;i<WW_MAX;i++)
    WHOWASHASH[i] = NULL;        
}


static void add_whowas_to_clist(aWhowas **bucket,aWhowas *whowas)
{
  whowas->cprev = NULL;
  if ((whowas->cnext = *bucket) != NULL)
    whowas->cnext->cprev = whowas;
  *bucket = whowas;
}
 
static void    del_whowas_from_clist(aWhowas **bucket, aWhowas *whowas)
{
  if (whowas->cprev)
    whowas->cprev->cnext = whowas->cnext;
  else
    *bucket = whowas->cnext;
  if (whowas->cnext)
    whowas->cnext->cprev = whowas->cprev;
}

static void add_whowas_to_list(aWhowas **bucket, aWhowas *whowas)
{
  whowas->prev = NULL;
  if ((whowas->next = *bucket) != NULL)
    whowas->next->prev = whowas;
  *bucket = whowas;
}
 
static void    del_whowas_from_list(aWhowas **bucket, aWhowas *whowas)
{
  if (whowas->prev)
    whowas->prev->next = whowas->next;
  else
    *bucket = whowas->next;
  if (whowas->next)
    whowas->next->prev = whowas->prev;
}
