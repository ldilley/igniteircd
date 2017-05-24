/*
 *   IRC - Internet Relay Chat, src/class.c
 *   Copyright (C) 1990 Darren Reed
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
 *   $Id: class.c,v 1.1.1.1 2006/03/08 23:28:08 malign Exp $
 */
#include "class.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_conf.h"
#include "send.h"
#include "struct.h"
#include "s_debug.h"

#define BAD_CONF_CLASS          -1
#define BAD_PING                -2
#define BAD_CLIENT_CLASS        -3

aClass* ClassList;

int     get_conf_class(aConfItem *aconf)
{
  if ((aconf) && ClassPtr(aconf))
    return (ConfClassType(aconf));

  Debug((DEBUG_DEBUG,"No Class For %s",
         (aconf) ? aconf->name : "*No Conf*"));

  return (BAD_CONF_CLASS);

}

static  int     get_conf_ping(aConfItem *aconf)
{
  if ((aconf) && ClassPtr(aconf))
    return (ConfPingFreq(aconf));

  Debug((DEBUG_DEBUG,"No Ping For %s",
         (aconf) ? aconf->name : "*No Conf*"));

  return (BAD_PING);
}

int     get_client_class(aClient *acptr)
{
  Link  *tmp;
  aClass        *cl;
  int   retc = BAD_CLIENT_CLASS;

  if (acptr && !IsMe(acptr)  && (acptr->confs))
    for (tmp = acptr->confs; tmp; tmp = tmp->next)
      {
        if (!tmp->value.aconf ||
            !(cl = ClassPtr(tmp->value.aconf)))
          continue;
        if (ClassType(cl) > retc)
          retc = ClassType(cl);
      }

  Debug((DEBUG_DEBUG,"Returning Class %d For %s",retc,acptr->name));

  return (retc);
}

int     get_client_ping(aClient *acptr)
{
  int   ping = 0, ping2;
  aConfItem     *aconf;
  Link  *link;

  link = acptr->confs;

  if (link)
    while (link)
      {
        aconf = link->value.aconf;
        if (aconf->status & (CONF_CLIENT|CONF_CONNECT_SERVER|
                             CONF_NOCONNECT_SERVER))
          {
            ping2 = get_conf_ping(aconf);
            if ((ping2 != BAD_PING) && ((ping > ping2) ||
                                        !ping))
              ping = ping2;
          }
        link = link->next;
      }
  else
    {
      ping = PINGFREQUENCY;
      Debug((DEBUG_DEBUG,"No Attached Confs"));
    }
  if (ping <= 0)
    ping = PINGFREQUENCY;
  Debug((DEBUG_DEBUG,"Client %s Ping %d", acptr->name, ping));
  return (ping);
}

int     get_con_freq(aClass *clptr)
{
  if (clptr)
    return (ConFreq(clptr));
  else
    return (CONNECTFREQUENCY);
}

/*
 * When adding a class, check to see if it is already present first.
 * if so, then update the information for that class, rather than create
 * a new entry for it and later delete the old entry.
 * if no present entry is found, then create a new one and add it in
 * immediately after the first one (class 0).
 */
void    add_class(int class,
                  int ping,
                  int confreq,
                  int maxli,
                  long sendq)
{
  aClass *t, *p;

  t = find_class(class);
  if ((t == ClassList) && (class != 0))
    {
      p = (aClass *)make_class();
      p->next = t->next;
      t->next = p;
    }
  else
    p = t;
  Debug((DEBUG_DEBUG,
         "Add Class %d: p %x t %x - cf: %d pf: %d ml: %d sq: %l",
         class, p, t, confreq, ping, maxli, sendq));
  ClassType(p) = class;
  ConFreq(p) = confreq;
  PingFreq(p) = ping;
  MaxLinks(p) = maxli;
  MaxSendq(p) = (sendq > 0) ? sendq : MAXSENDQLENGTH;
  if (p != t)
    Links(p) = 0;
}

aClass  *find_class(int cclass)
{
  aClass *cltmp;
  
  for (cltmp = ClassList; cltmp; cltmp = cltmp->next)
    if (ClassType(cltmp) == cclass)
      return cltmp;
  return ClassList;
}

void    check_class()
{
  struct Class *cltmp, *cltmp2;

  Debug((DEBUG_DEBUG, "Class check:"));

  for (cltmp2 = cltmp = ClassList; cltmp; cltmp = cltmp2->next)
    {
      Debug((DEBUG_DEBUG,
             "Class %d : CF: %d PF: %d ML: %d LI: %d SQ: %ld",
             ClassType(cltmp), ConFreq(cltmp), PingFreq(cltmp),
             MaxLinks(cltmp), Links(cltmp), MaxSendq(cltmp)));
      if (MaxLinks(cltmp) < 0)
        {
          cltmp2->next = cltmp->next;
          if (Links(cltmp) <= 0)
            free_class(cltmp);
        }
      else
        cltmp2 = cltmp;
    }
}

void    initclass()
{
  ClassList = (aClass *)make_class();

  ClassType(ClassList) = 0;
  ConFreq(ClassList) = CONNECTFREQUENCY;
  PingFreq(ClassList) = PINGFREQUENCY;
  MaxLinks(ClassList) = MAXIMUM_LINKS;
  MaxSendq(ClassList) = MAXSENDQLENGTH;
  Links(ClassList) = 0;
  ClassList->next = NULL;
}

void    report_classes(aClient *sptr)
{
  aClass *cltmp;

  for (cltmp = ClassList; cltmp; cltmp = cltmp->next)
    sendto_one(sptr, form_str(RPL_STATSYLINE), me.name, sptr->name,
               'Y', ClassType(cltmp), PingFreq(cltmp), ConFreq(cltmp),
               MaxLinks(cltmp), MaxSendq(cltmp));
}

long    get_sendq(aClient *cptr)
{
  int   sendq = MAXSENDQLENGTH, retc = BAD_CLIENT_CLASS;
  Link  *tmp;
  struct Class        *cl;

  if (cptr && !IsMe(cptr)  && (cptr->confs))
    for (tmp = cptr->confs; tmp; tmp = tmp->next)
      {
        if (!tmp->value.aconf ||
            !(cl = ClassPtr(tmp->value.aconf)))
          continue;
        if (ClassType(cl) > retc)
          sendq = MaxSendq(cl);
      }
  return sendq;
}


