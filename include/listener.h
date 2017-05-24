/* - Internet Relay Chat, include/listener.h
 *   Copyright (C) 1999 Thomas Helvey <tomh@inxpress.net>
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
 *
 * $Id: listener.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 */
#ifndef INCLUDED_listener_h
#define INCLUDED_listener_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* size_t */
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>      /* in_addr */
#define INCLUDED_netinet_in_h
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"       /* HOSTLEN */
#endif

struct Client;

struct Listener {
  struct Listener* next;               /* list node pointer */
  const char*      name;               /* listener name */
  int              fd;                 /* file descriptor */
  int              port;               /* listener IP port */
  int              ref_count;          /* number of connection references */
  int              active;             /* current state of listener */
  int              index;              /* index into poll array */
  time_t           last_accept;        /* last time listener accepted */
  struct in_addr   addr;               /* virtual address or INADDR_ANY */
  char             vhost[HOSTLEN + 1]; /* virtual name of listener */
};

extern struct Listener* ListenerPollList; /* GLOBAL - listener list */

extern void        accept_connection(struct Listener* listener);
extern void        add_listener(int port, const char* vaddr_ip);
extern void        close_listener(struct Listener* listener);
extern void        close_listeners(void);
extern void        free_listener(struct Listener* listener);
extern const char* get_listener_name(const struct Listener* listener);
extern struct Listener* make_listener(int port, struct in_addr addr);
extern void        mark_listeners_closing(void);
extern void        show_ports(struct Client* client);

#endif /* INCLUDED_listener_h */
