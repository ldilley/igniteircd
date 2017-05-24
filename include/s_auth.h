/************************************************************************
 *   IRC - Internet Relay Chat, include/s_auth.h
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
 *   $Id: s_auth.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */
#ifndef INCLUDED_s_auth_h
#define INCLUDED_s_auth_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_config_h
#include "config.h"
#endif

struct Client;

struct AuthRequest {
  struct AuthRequest* next;      /* linked list node ptr */
  struct AuthRequest* prev;      /* linked list node ptr */
  struct Client*      client;    /* pointer to client struct for request */
  unsigned int        flags;     /* current state of request */
  int                 fd;        /* file descriptor for auth queries */
  int                 index;     /* select / poll index */
  time_t              timeout;   /* time when query expires */
};

/*
 * flag values for AuthRequest
 * NAMESPACE: AM_xxx - Authentication Module
 */
#define AM_AUTH_CONNECTING   (1 << 0)
#define AM_AUTH_PENDING      (1 << 1)
#define AM_DNS_PENDING       (1 << 2)

#define SetDNSPending(x)     ((x)->flags |= AM_DNS_PENDING)
#define ClearDNSPending(x)   ((x)->flags &= ~AM_DNS_PENDING)
#define IsDNSPending(x)      ((x)->flags &  AM_DNS_PENDING)

#define SetAuthConnect(x)    ((x)->flags |= AM_AUTH_CONNECTING)
#define ClearAuthConnect(x)  ((x)->flags &= ~AM_AUTH_CONNECTING)
#define IsAuthConnect(x)     ((x)->flags &  AM_AUTH_CONNECTING)

#define SetAuthPending(x)    ((x)->flags |= AM_AUTH_PENDING)
#define ClearAuthPending(x)  ((x)->flags &= AM_AUTH_PENDING)
#define IsAuthPending(x)     ((x)->flags &  AM_AUTH_PENDING)

#define ClearAuth(x)         ((x)->flags &= ~(AM_AUTH_PENDING | AM_AUTH_CONNECTING))
#define IsDoingAuth(x)       ((x)->flags &  (AM_AUTH_PENDING | AM_AUTH_CONNECTING))
/* #define SetGotId(x)       ((x)->flags |= FLAGS_GOTID) */


extern struct AuthRequest* AuthPollList;  /* GLOBAL - auth queries pending io */

extern void start_auth(struct Client *);
extern void timeout_auth_queries(time_t now);
extern void read_auth_reply(struct AuthRequest* req);
extern void send_auth_query(struct AuthRequest* req);
extern void free_auth_request(struct AuthRequest* request);

#endif /* INCLUDED_s_auth_h */

