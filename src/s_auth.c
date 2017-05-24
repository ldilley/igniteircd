/************************************************************************
 *   IRC - Internet Relay Chat, src/s_auth.c
 *   Copyright (C) 1992 Darren Reed
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
 *   $Id: s_auth.c,v 1.3 2007/02/14 11:21:39 malign Exp $
 *
 * Changes:
 *   July 6, 1999 - Rewrote most of the code here. When a client connects
 *     to the server and passes initial socket validation checks, it
 *     is owned by this module (auth) which returns it to the rest of the
 *     server when dns and auth queries are finished. Until the client is
 *     released, the server does not know it exists and does not process
 *     any messages from it.
 *     --Bleep  Thomas Helvey <tomh@inxpress.net>
 */
#include "m_commands.h"
#include "s_auth.h"
#include "client.h"
#include "common.h"
#include "fdlist.h"              /* fdlist_add */
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "res.h"
#include "s_bsd.h"
#include "s_log.h"
#include "s_stats.h"
#include "send.h"
#include "struct.h"

#include <netdb.h>               /* struct hostent */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>

/*
 * a bit different approach
 * this replaces the original sendheader macros
 */
static struct {
  const char* message;
  size_t      length;
} HeaderMessages [] = {
  /* 123456789012345678901234567890123456789012345678901234567890 */
  { "NOTICE AUTH :*** Looking up your hostname...\r\n",      46 },
  { "NOTICE AUTH :*** Found your hostname.\r\n",             39 },
  { "NOTICE AUTH :*** Found your hostname (cached).\r\n",    48 },
  { "NOTICE AUTH :*** Unable to look up your hostname!\r\n", 51 },
  { "NOTICE AUTH :*** Checking Ident...\r\n",                36 },
  { "NOTICE AUTH :*** Got Ident response.\r\n",              38 },
  { "NOTICE AUTH :*** No Ident response!\r\n",               37 },
  { "NOTICE AUTH :*** Your forward and reverse DNS do not match! " \
    "Ignoring hostname.\r\n",                                80 },
  { "NOTICE AUTH :*** Your hostname is too long! Ignoring hostname.\r\n ", 64 }
};

typedef enum {
  REPORT_DO_DNS,
  REPORT_FIN_DNS,
  REPORT_FIN_DNSC,
  REPORT_FAIL_DNS,
  REPORT_DO_ID,
  REPORT_FIN_ID,
  REPORT_FAIL_ID,
  REPORT_IP_MISMATCH,
  REPORT_HOST_TOOLONG
} ReportType;

#define sendheader(c, r) \
   send((c)->fd, HeaderMessages[(r)].message, HeaderMessages[(r)].length, 0)

struct AuthRequest* AuthPollList = 0; /* GLOBAL - auth queries pending io */

static struct AuthRequest* AuthIncompleteList = 0;

/*
 * make_auth_request - allocate a new auth request
 */
static struct AuthRequest* make_auth_request(struct Client* client)
{
  /*
   * XXX - use blalloc here?
   */
  struct AuthRequest* request = 
               (struct AuthRequest*) MyMalloc(sizeof(struct AuthRequest));
  assert(0 != request);
  memset(request, 0, sizeof(struct AuthRequest));
  request->fd      = -1;
  request->client  = client;
  request->timeout = CurrentTime + CONNECTTIMEOUT;
  return request;
}

/*
 * free_auth_request - cleanup auth request allocations
 */
void free_auth_request(struct AuthRequest* request)
{
  /*
   * XXX - use blfree here?
   */
  MyFree(request);
}

/*
 * unlink_auth_request - remove auth request from a list
 */
static void unlink_auth_request(struct AuthRequest* request,
                                struct AuthRequest** list)
{
  if (request->next)
    request->next->prev = request->prev;
  if (request->prev)
    request->prev->next = request->next;
  else
    *list = request->next;
}

/*
 * link_auth_request - add auth request to a list
 */
static void link_auth_request(struct AuthRequest* request,
                              struct AuthRequest** list)
{
  request->prev = 0;
  request->next = *list;
  if (*list)
    (*list)->prev = request;
  *list = request;
}

/*
 * release_auth_client - release auth client from auth system
 * this adds the client into the local client lists so it can be read by
 * the main io processing loop
 */
static void release_auth_client(struct Client* client)
{
  if (client->fd > highest_fd)
    highest_fd = client->fd;
  local[client->fd] = client;

  fdlist_add(client->fd, FDL_DEFAULT);
  add_client_to_list(client);
  
  SetAccess(client);
}
 
/*
 * auth_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * set the client on it's way to a connection completion, regardless
 * of success of failure
 */
static void auth_dns_callback(void* vptr, adns_answer* reply)
{
  struct AuthRequest* auth = (struct AuthRequest*) vptr;

  ClearDNSPending(auth);
  if (reply && (reply->status == adns_s_ok))
  {
      if(strlen(*reply->rrs.str) < HOSTLEN)
      {
        strcpy(auth->client->host, *reply->rrs.str);
        sendheader(auth->client, REPORT_FIN_DNS);
      } else {
        sendheader(auth->client, REPORT_HOST_TOOLONG);
      }
  } else
  {
     strcpy(auth->client->host, auth->client->sockhost);
     sendheader(auth->client, REPORT_FAIL_DNS);
  }
  MyFree(reply);
    
  auth->client->dns_query = NULL;
  auth->client->host[HOSTLEN] = '\0';
  if (!IsDoingAuth(auth))
    {
      release_auth_client(auth->client);
      unlink_auth_request(auth, &AuthIncompleteList);
      free_auth_request(auth);
    }
}

/*
 * authsenderr - handle auth send errors
 */
static void auth_error(struct AuthRequest* auth)
{
  ++ServerStats->is_abad;

  close(auth->fd);
  auth->fd = -1;

  ClearAuth(auth);
  sendheader(auth->client, REPORT_FAIL_ID);

  unlink_auth_request(auth, &AuthPollList);

  if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else
    {
      release_auth_client(auth->client);
      free_auth_request(auth);
    }
}

/*
 * start_auth_query - Flag the client to show that an attempt to 
 * contact the ident server on
 * the client's host.  The connect and subsequently the socket are all put
 * into 'non-blocking' mode.  Should the connect or any later phase of the
 * identifing process fail, it is aborted and the user is given a username
 * of "unknown".
 */
static int start_auth_query(struct AuthRequest* auth)
{
  struct sockaddr_in sock;
  struct sockaddr_in localaddr;
  int                locallen = sizeof(struct sockaddr_in);
  int                fd;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
      /* er .. on the off chance we're having ident errors, we may leak a
       * server ip to a +d user. on the other hand, we should be spilling
       * such a large amount of ident errors on a busy server that the
       * server ip will be missed, and also there is no way that the person
       * seeing the notice would know that that ip was the server ..
       * we should change this sometime .... -gnp */
      report_error("creating auth stream socket %s:%s", 
		   get_client_name(auth->client, TRUE), errno);
      Log("Unable to create auth socket for %s:%m",
	  get_client_name(auth->client, SHOW_IP));
      ++ServerStats->is_abad;
      return 0;
    }
  if ((MAXCONNECTIONS - 10) < fd)
    {
	  /* see my note on ip leakage above -gnp */
      sendto_realops("Can't allocate fd for auth on %s",
		     get_client_name(auth->client, TRUE));
      
      close(fd);
      return 0;
    }

  sendheader(auth->client, REPORT_DO_ID);
  if (!set_non_blocking(fd)) {
    report_error(NONB_ERROR_MSG, get_client_name(auth->client, SHOW_IP), errno);
    close(fd);
    return 0;
  }

  /* 
   * get the local address of the client and bind to that to
   * make the auth request.  This used to be done only for
   * ifdef VIRTTUAL_HOST, but needs to be done for all clients
   * since the ident request must originate from that same address--
   * and machines with multiple IP addresses are common now
   */
  memset(&localaddr, 0, locallen);
  getsockname(auth->client->fd, (struct sockaddr*) &localaddr, &locallen);
  localaddr.sin_port = htons(0);

  if (bind(fd, (struct sockaddr*) &localaddr, sizeof(localaddr)))
    {
	  /* see my note above -gnp */
      report_error("binding auth stream socket %s:%s", 
		   get_client_name(auth->client, TRUE), errno);
      close(fd);
      return 0;
    }

  memcpy(&sock.sin_addr, &auth->client->ip, sizeof(struct in_addr));
  
  sock.sin_port = htons(113);
  sock.sin_family = AF_INET;

  if (connect(fd, (struct sockaddr*) &sock, sizeof(sock)) == -1)
    {
      if (errno != EINPROGRESS)
	{
	  ServerStats->is_abad++;
	  /*
	   * No error report from this...
	   */
	  close(fd);
	  sendheader(auth->client, REPORT_FAIL_ID);
	  return 0;
	}
    }

  auth->fd = fd;

  SetAuthConnect(auth);
  return 1;
}

/*
 * GetValidIdent - parse ident query reply from identd server
 * 
 * Inputs        - pointer to ident buf
 * Output        - NULL if no valid ident found, otherwise pointer to name
 * Side effects        -
 */
static char* GetValidIdent(char *buf)
{
  int   remp = 0;
  int   locp = 0;
  char* colon1Ptr;
  char* colon2Ptr;
  char* colon3Ptr;
  char* commaPtr;
  char* remotePortString;

  /* All this to get rid of a sscanf() fun. */
  remotePortString = buf;
  
  colon1Ptr = strchr(remotePortString,':');
  if(!colon1Ptr)
    return 0;

  *colon1Ptr = '\0';
  colon1Ptr++;
  colon2Ptr = strchr(colon1Ptr,':');
  if(!colon2Ptr)
    return 0;

  *colon2Ptr = '\0';
  colon2Ptr++;
  commaPtr = strchr(remotePortString, ',');

  if(!commaPtr)
    return 0;

  *commaPtr = '\0';
  commaPtr++;

  remp = atoi(remotePortString);
  if(!remp)
    return 0;
              
  locp = atoi(commaPtr);
  if(!locp)
    return 0;

  /* look for USERID bordered by first pair of colons */
  if(!strstr(colon1Ptr, "USERID"))
    return 0;

  colon3Ptr = strchr(colon2Ptr,':');
  if(!colon3Ptr)
    return 0;
  
  *colon3Ptr = '\0';
  colon3Ptr++;
  return(colon3Ptr);
}

/*
 * start_auth - starts auth (identd) and dns queries for a client
 */
void start_auth(struct Client* client)
{
  struct AuthRequest* auth = 0;

  assert(0 != client);

  auth = make_auth_request(client);

  client->dns_query = MyMalloc(sizeof(struct DNSQuery));
  client->dns_query->ptr     = auth;
  client->dns_query->callback = auth_dns_callback;

  sendheader(client, REPORT_DO_DNS);

  adns_getaddr(&client->ip, client->dns_query);
  SetDNSPending(auth);

  if (start_auth_query(auth))
    link_auth_request(auth, &AuthPollList);
  else if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else
    {
      free_auth_request(auth);
      release_auth_client(client);
    }
}

/*
 * timeout_auth_queries - timeout resolver and identd requests
 * allow clients through if requests failed
 */
void timeout_auth_queries(time_t now)
{
  struct AuthRequest* auth;
  struct AuthRequest* auth_next = 0;

  for (auth = AuthPollList; auth; auth = auth_next)
    {
      auth_next = auth->next;
      if (auth->timeout < CurrentTime)
	{
	  if (-1 < auth->fd)
	    close(auth->fd);

	  sendheader(auth->client, REPORT_FAIL_ID);
	  if (IsDNSPending(auth))
	    {
	      delete_adns_queries(auth->client->dns_query);
	      auth->client->dns_query->query = NULL;
	      sendheader(auth->client, REPORT_FAIL_DNS);
	    }
	  Log("DNS/AUTH timeout %s",
	      get_client_name(auth->client, SHOW_IP));

	  auth->client->since = now;
	  release_auth_client(auth->client);
	  unlink_auth_request(auth, &AuthPollList);
	  free_auth_request(auth);
	}
    }
  for (auth = AuthIncompleteList; auth; auth = auth_next)
    {
      auth_next = auth->next;
      if (auth->timeout < CurrentTime)
	{
	  delete_adns_queries(auth->client->dns_query);
	  auth->client->dns_query->query = NULL;
	  sendheader(auth->client, REPORT_FAIL_DNS);
	  Log("DNS timeout %s", get_client_name(auth->client, SHOW_IP));

	  auth->client->since = now;
	  release_auth_client(auth->client);
	  unlink_auth_request(auth, &AuthIncompleteList);
	  free_auth_request(auth);
	}
    }
}

/*
 * send_auth_query - send the ident server a query giving "theirport , ourport"
 * The write is only attempted *once* so it is deemed to be a fail if the
 * entire write doesn't write all the data given.  This shouldnt be a
 * problem since the socket should have a write buffer far greater than
 * this message to store it in should problems arise. -avalon
 */
void send_auth_query(struct AuthRequest* auth)
{
  struct sockaddr_in us;
  struct sockaddr_in them;
  char            authbuf[32];
  int             ulen = sizeof(struct sockaddr_in);
  int             tlen = sizeof(struct sockaddr_in);

  if (getsockname(auth->client->fd, (struct sockaddr *)&us,   &ulen) ||
      getpeername(auth->client->fd, (struct sockaddr *)&them, &tlen))
    {
      
      Log("auth get{sock,peer}name error for %s:%m",
	  get_client_name(auth->client, SHOW_IP));
      auth_error(auth);
      return;
    }
  ircsprintf(authbuf, "%u , %u\r\n",
             (unsigned int) ntohs(them.sin_port),
             (unsigned int) ntohs(us.sin_port));

  if (send(auth->fd, authbuf, strlen(authbuf), 0) == -1)
    {
      if (EAGAIN == errno)
	return;
      auth_error(auth);
      return;
    }
  ClearAuthConnect(auth);
  SetAuthPending(auth);
}


/*
 * read_auth_reply - read the reply (if any) from the ident server 
 * we connected to.
 * We only give it one shot, if the reply isn't good the first time
 * fail the authentication entirely. --Bleep
 */
#define AUTH_BUFSIZ 128

void read_auth_reply(struct AuthRequest* auth)
{
  char* s=(char *)NULL;
  char* t=(char *)NULL;
  int   len;
  int   count;
  char  buf[AUTH_BUFSIZ + 1]; /* buffer to read auth reply into */

  len = recv(auth->fd, buf, AUTH_BUFSIZ, 0);
  
  if (len > 0)
    {
      buf[len] = '\0';

      if( (s = GetValidIdent(buf)) )
	{
	  t = auth->client->username;
	  for (count = USERLEN; *s && count; s++)
	    {
	      if(*s == '@')
		{
		  break;
		}
	      if ( !IsSpace(*s) && *s != ':' )
		{
		  *t++ = *s;
		  count--;
		}
	    }
	  *t = '\0';
	}
    }

  if ((len < 0) && (EAGAIN == errno))
    return;

  close(auth->fd);
  auth->fd = -1;
  ClearAuth(auth);
  
  if (!s)
    {
      ++ServerStats->is_abad;
      strcpy(auth->client->username, "unknown");
    }
  else
    {
      sendheader(auth->client, REPORT_FIN_ID);
      ++ServerStats->is_asuc;
      SetGotId(auth->client);
    }
  unlink_auth_request(auth, &AuthPollList);

  if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else
    {
      release_auth_client(auth->client);
      free_auth_request(auth);
    }
}

