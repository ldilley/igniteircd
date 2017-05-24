/************************************************************************
 *   IRC - Internet Relay Chat, include/class.h
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
 */
/* $Id: class.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $ */

#ifndef INCLUDED_class_h
#define INCLUDED_class_h

struct ConfItem;
struct Client;

struct Class {
  struct Class* next;     /* list node pointer */
  int           type;
  int           conFreq;
  int           pingFreq;
  int           maxLinks;
  long          maxSendq;
  int           links;
};

typedef struct Class aClass;

#define ClassType(x)    ((x)->type)
#define ConFreq(x)      ((x)->conFreq)
#define PingFreq(x)     ((x)->pingFreq)
#define MaxLinks(x)     ((x)->maxLinks)
#define MaxSendq(x)     ((x)->maxSendq)
#define Links(x)        ((x)->links)

#define ClassPtr(x)      ((x)->c_class)
#define ConfLinks(x)     (ClassPtr(x)->links)
#define ConfMaxLinks(x)  (ClassPtr(x)->maxLinks)
#define ConfClassType(x) (ClassPtr(x)->type)
#define ConfConFreq(x)   (ClassPtr(x)->conFreq)
#define ConfPingFreq(x)  (ClassPtr(x)->pingFreq)
#define ConfSendq(x)     (ClassPtr(x)->maxSendq)

extern struct Class* ClassList;  /* GLOBAL - class list */

extern  long    get_sendq(struct Client *);
extern  int     get_con_freq(struct Class* );
extern  aClass  *find_class(int);
extern  int     get_conf_class (struct ConfItem *);
extern  int     get_client_class (struct Client *);
extern  int     get_client_ping (struct Client *);
extern  void    add_class(int, int, int, int, long);
extern  void    check_class(void);
extern  void    initclass(void);
extern  void    free_class(struct Class* );
extern  void    add_class (int, int, int, int, long);
extern  void    fix_class (struct ConfItem *, struct ConfItem *);
extern  void    report_classes (struct Client *);

#endif /* INCLUDED_class_h */
