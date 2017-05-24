/************************************************************************
 *   IRC - Internet Relay Chat, src/s_bsd.c
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
 *  $Id: s_bsd.c,v 1.3 2007/02/14 11:21:40 malign Exp $
 */
#include "s_bsd.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "fdlist.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "numeric.h"
#include "packet.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"
#include "s_stats.h"
#include "s_zip.h"
#include "send.h"
#include "struct.h"

#ifdef DEBUGMODE
#include "s_debug.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/param.h>    /* NOFILE */
#include <arpa/inet.h>

/*
 * Stuff for poll()
 */
#ifdef USE_POLL
#include <sys/poll.h>
#define CONNECTFAST
#endif

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

extern struct sockaddr_in vserv;               /* defined in s_conf.c */

const char* const NONB_ERROR_MSG   = "set_non_blocking failed for %s:%s"; 
const char* const OPT_ERROR_MSG    = "disable_sock_options failed for %s:%s";
const char* const SETBUF_ERROR_MSG = "set_sock_buffers failed for server %s:%s";

struct Client* local[MAXCONNECTIONS];

int            highest_fd = 0;

static struct sockaddr_in mysk;
static char readBuf[READBUF_SIZE];
#ifndef USE_POLL
/*
 * Stuff for select()
 */

static fd_set readSet;
static fd_set writeSet;

fd_set*  read_set  = &readSet;
fd_set*  write_set = &writeSet;

#endif /* USE_POLL */

void close_all_connections(void)
{
  int i;
  for (i = 0; i < MAXCONNECTIONS; ++i) {
    close(i);
    local[i] = 0;
  }
}

void init_netio(void)
{
#ifndef USE_POLL
  read_set  = &readSet;
  write_set = &writeSet;
#endif
  init_resolver();
}
 
/*
 * get_sockerr - get the error value from the socket or the current errno
 *
 * Get the *real* error from the socket (well try to anyway..).
 * This may only work when SO_DEBUG is enabled but its worth the
 * gamble anyway.
 */
static int get_sockerr(int fd)
{
  int errtmp = errno;
#ifdef SO_ERROR
  int err = 0;
  int len = sizeof(err);

  if (-1 < fd && !getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*) &err, &len)) {
    if (err)
      errtmp = err;
  }
  errno = errtmp;
#endif
  return errtmp;
}

/*
 * report_error - report an error from an errno. 
 * Record error to log and also send a copy to all *LOCAL* opers online.
 *
 *        text        is a *format* string for outputing error. It must
 *                contain only two '%s', the first will be replaced
 *                by the sockhost from the cptr, and the latter will
 *                be taken from sys_errlist[errno].
 *
 *        cptr        if not NULL, is the *LOCAL* client associated with
 *                the error.
 *
 * Cannot use perror() within daemon. stderr is closed in
 * ircd and cannot be used. And, worse yet, it might have
 * been reassigned to a normal connection...
 * 
 * Actually stderr is still there IFF ircd was run with -s --Rodder
 */

void report_error(const char* text, const char* who, int error) 
{
  who = (who) ? who : "";

  sendto_realops_flags(FLAGS_DEBUG, text, who, strerror(error));

  Log(text, who, strerror(error));
}

/*
 * connect_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, reply will contain
 * a non-null pointer, otherwise reply will be null.
 * if successful start the connection, otherwise notify opers
 */
static void connect_dns_callback(void* vptr, adns_answer* reply)
{
  struct ConfItem* aconf = (struct ConfItem*) vptr;
  aconf->dns_pending = 0;
  if (reply && reply->status == adns_s_ok) {
    aconf->ipnum.s_addr = reply->rrs.addr->addr.inet.sin_addr.s_addr;
    MyFree(reply);
    connect_server(aconf, 0, NULL);
  }
  else
    sendto_realops("Connect to %s failed: host lookup", aconf->host);
}

/*
 * set_sock_buffers - set send and receive buffers for socket
 * returns true (1) if successful, false (0) otherwise
 */
int set_sock_buffers(int fd, int size)
{
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*) &size, sizeof(size)) ||
      setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size)))
    return 0;
  return 1;
}

/*
 * disable_sock_options - if remote has any socket options set, disable them 
 * returns true (1) if successful, false (0) otherwise
 */
static int disable_sock_options(int fd)
{
#if defined(IP_OPTIONS) && defined(IPPROTO_IP)
  if (setsockopt(fd, IPPROTO_IP, IP_OPTIONS, NULL, 0))
    return 0;
#endif
  return 1;
}

/*
 * set_non_blocking - Set the client connection into non-blocking mode. 
 * If your system doesn't support this, you're screwed, ircd will run like
 * crap.
 * returns true (1) if successful, false (0) otherwise
 */
int set_non_blocking(int fd)
{
  /*
   * NOTE: consult ALL your relevant manual pages *BEFORE* changing
   * these ioctl's.  There are quite a few variations on them,
   * as can be seen by the PCS one.  They are *NOT* all the same.
   * Heed this well. - Avalon.
   */
  /* This portion of code might also apply to NeXT.  -LynX */
#ifdef NBLOCK_SYSV
  int res = 1;

  if (ioctl(fd, FIONBIO, &res) == -1)
    return 0;

#else /* !NBLOCK_SYSV */
  int nonb = 0;
  int res;

#ifdef NBLOCK_POSIX
  nonb |= O_NONBLOCK;
#endif
#ifdef NBLOCK_BSD
  nonb |= O_NDELAY;
#endif

  res = fcntl(fd, F_GETFL, 0);
  if (-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
    return 0;
#endif /* !NBLOCK_SYSV */
  return 1;
}

/*
 * deliver_it
 *      Attempt to send a sequence of bytes to the connection.
 *      Returns
 *
 *      < 0     Some fatal error occurred, (but not EWOULDBLOCK).
 *              This return is a request to close the socket and
 *              clean up the link.
 *      
 *      >= 0    No real error occurred, returns the number of
 *              bytes actually transferred. EWOULDBLOCK and other
 *              possibly similar conditions should be mapped to
 *              zero return. Upper level routine will have to
 *              decide what to do with those unwritten bytes...
 *
 *      *NOTE*  alarm calls have been preserved, so this should
 *              work equally well whether blocking or non-blocking
 *              mode is used...
 */
int deliver_it(aClient* cptr, const char* str, int len)
{
  int   retval;

  retval = send(cptr->fd, str, len, 0);
  /*
  ** Convert WOULDBLOCK to a return of "0 bytes moved". This
  ** should occur only if socket was non-blocking. Note, that
  ** all is Ok, if the 'write' just returns '0' instead of an
  ** error and errno=EWOULDBLOCK.
  **
  */
  if (retval < 0 && (errno == EWOULDBLOCK || errno == EAGAIN ||
                     errno == ENOBUFS))
    {
      retval = 0;
      cptr->flags |= FLAGS_BLOCKED;
      return(retval);  /* Just get out now... */
    }
  else if (retval > 0)
    {
      cptr->flags &= ~FLAGS_BLOCKED;
    }

  if (retval > 0)
    {
      cptr->sendB += retval;
      me.sendB += retval;
      if (cptr->sendB > 1023)
        {
          cptr->sendK += (cptr->sendB >> 10);
          cptr->sendB &= 0x03ff;        /* 2^10 = 1024, 3ff = 1023 */
        }
      else if (me.sendB > 1023)
        {
          me.sendK += (me.sendB >> 10);
          me.sendB &= 0x03ff;
        }
    }
  return(retval);
}


/*
 * completed_connection - Complete non-blocking connect-sequence. 
 * Check access and terminate connection, if trouble detected.
 *
 * Return         TRUE, if successfully completed
 *                FALSE, if failed and ClientExit
 */
static int completed_connection(struct Client* cptr)
{
  struct ConfItem* c_conf;
  struct ConfItem* n_conf;
  c_conf = find_conf_name(cptr->confs, cptr->name, CONF_CONNECT_SERVER);
  if (!c_conf)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Lost C-Line for %s", get_client_name(cptr,MASK_IP));
#else      
      sendto_realops("Lost C-Line for %s", get_client_name(cptr,FALSE));
#endif      
      return 0;
    }
  n_conf = find_conf_name(cptr->confs, cptr->name, CONF_NOCONNECT_SERVER);
  if (!n_conf)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Lost N-Line for %s", get_client_name(cptr, MASK_IP));
#else      
      sendto_realops("Lost N-Line for %s", get_client_name(cptr,FALSE));
#endif      
      return 0;
    }
  
  SetHandshake(cptr);
    if (!EmptyString(c_conf->passwd))
    {
      sendto_one(cptr, "PASS %s :TS+Ignite", c_conf->passwd);
    }
    send_capabilities(cptr, (c_conf->flags & CONF_FLAGS_ZIP_LINK));
    sendto_one(cptr, "SERVER %s 1 :%s",
               my_name_for_link(me.name, n_conf), me.info);
    return (IsDead(cptr)) ? 0 : 1;
}

/*
 * connect_inet - open a socket and connect to another server
 * returns true (1) if successful, false (0) otherwise
 */
static int connect_inet(struct ConfItem *aconf, struct Client *cptr)
{
  static struct sockaddr_in con_sin;
  assert(0 != aconf);
  assert(0 != cptr);
  /*
   * Might as well get sockhost from here, the connection is attempted
   * with it so if it fails its useless.
   */
  cptr->fd = socket(AF_INET, SOCK_STREAM, 0);

  if (cptr->fd == -1)
    {
      report_error("opening stream socket to server %s:%s", cptr->name, errno);
      return 0;
    }

  if (cptr->fd >= (HARD_FDLIMIT - 10))
    {
      sendto_realops("No more connections allowed (%s)", cptr->name);
      return 0;
    }

  mysk.sin_port   = 0;
  mysk.sin_family = AF_INET;

  memset(&con_sin, 0, sizeof(con_sin));
  con_sin.sin_family  = AF_INET;

  /*
   * Bind to a local IP# (with unknown port - let unix decide) so
   * we have some chance of knowing the IP# that gets used for a host
   * with more than one IP#.
   * 
   * No we don't bind it, not all OS's can handle connecting with
   * an already bound socket, different ip# might occur anyway
   * leading to a freezing select() on this side for some time.
   */
  /* this is code duplication, i know, but i can't think of a better way
   * at 3am...
   */
  if (aconf->ip != 0)
    {
      mysk.sin_addr.s_addr = htonl(aconf->ip);

      /* support for specifying an address to bind to in C:
       * we stuff the ip in this conf field if there's a final
       * parameter in the c line, and we check if we found one (it would
       * be 0 if we didn't) and if so, bind to it.  otherwise, we let
       * the normal bind code handle it.
       */
      if (bind(cptr->fd, (struct sockaddr*)&mysk, sizeof(mysk)))
        {
          report_error("error binding to local port for %s:%s",
                       cptr->name, errno);
          return 0;
        }
    }
  else if (specific_virtual_host)
    {
      mysk.sin_addr = vserv.sin_addr;

      /*
       * No, we do bind it if we have virtual host support. If we don't
       * explicitly bind it, it will default to IN_ADDR_ANY and we lose
       * due to the other server not allowing our base IP --smg
       */        
      if (bind(cptr->fd, (struct sockaddr*) &mysk, sizeof(mysk)))
        {
          report_error("error binding to local port for %s:%s", 
                       cptr->name, errno);
          return 0;
        }
    }
  con_sin.sin_addr.s_addr = aconf->ipnum.s_addr;
  con_sin.sin_port        = htons(aconf->port);
  /*
   * save connect info in client
   */
  cptr->ip.s_addr     = aconf->ipnum.s_addr;
  cptr->port          = aconf->port;
  strncpy_irc(cptr->sockhost, inetntoa((const char*) &cptr->ip.s_addr), 
              HOSTIPLEN);

  if (!set_non_blocking(cptr->fd))
    report_error(NONB_ERROR_MSG, 
#ifdef HIDE_SERVERS_IPS
		get_client_name(cptr, MASK_IP),
#else		
    		get_client_name(cptr, TRUE), 
#endif		
		errno);

  if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
#ifdef HIDE_SERVERS_IPS
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, MASK_IP), errno);
#else    
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);
#endif    

  if (connect(cptr->fd, (struct sockaddr*) &con_sin, sizeof(con_sin)) && 
      errno != EINPROGRESS)
    {
      int errtmp = errno; /* other system calls may eat errno */

#ifdef HIDE_SERVERS_IPS      
      report_error("Connect to host %s failed: %s",
      		   get_client_name(cptr, MASK_IP), errno);
#else		   
      report_error("Connect to host %s failed: %s",
                   get_client_name(cptr, TRUE), errno);
#endif		   
      errno = errtmp;
      return 0;
    }
  return 1;
}

/*
 * connect_server - start or complete a connection to another server
 * returns true (1) if successful, false (0) otherwise
 *
 * aconf must point to a valid C:line
 * m_connect            calls this with a valid by client and a null reply
 * try_connections      calls this with a null by client, and a null reply
 * connect_dns_callback call this with a null by client, and a valid reply
 *
 * XXX - if this comes from an m_connect message and a dns query needs to
 * be done, we loose the information about who started the connection and
 * it's considered an auto connect.
 */
int connect_server(struct ConfItem* aconf, 
                   struct Client* by, struct DNSQuery *reply)
{
  struct Client* cptr;

  assert(0 != aconf);
  if (aconf->dns_pending)
    return 0;

  Log("Connect to %s[%s] @%s",
      aconf->user, aconf->host, inetntoa((char*)&aconf->ipnum));

  /*
   * if this is coming from m_connect, we have just checked this
   * NOTE: aconf should ALWAYS be a valid C:line
   */
  if ((cptr = find_server(aconf->name)))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Server %s already present from %s",
      		 aconf->name, get_client_name(cptr, MASK_IP));
      if (by && IsPerson(by) && !MyClient(by))
        sendto_one(by, ":%s NOTICE %s :Server %s already present from %s",
	           me.name, by->name, aconf->name,
		   get_client_name(cptr, MASK_IP));
#else		   
      sendto_realops("Server %s already present from %s",
                 aconf->name, get_client_name(cptr, TRUE));
      if (by && IsPerson(by) && !MyClient(by))
        sendto_one(by, ":%s NOTICE %s :Server %s already present from %s",
                   me.name, by->name, aconf->name,
                   get_client_name(cptr, TRUE));
#endif		   
      return 0;
    }

  /*
   * If we dont know the IP# for this host and it is a hostname and
   * not a ip# string, then try and find the appropriate host record.
   *
   * NOTE: if this is called from connect_dns_callback, the aconf ip
   * address will have been set so we don't need to worry about 
   * looping dns queries.
   */
  if (INADDR_NONE == aconf->ipnum.s_addr) {
    assert(0 == reply);
    if ((aconf->ipnum.s_addr = inet_addr(aconf->host)) == INADDR_NONE) {
      struct DNSQuery  *query;
      query = MyMalloc(sizeof(struct DNSQuery));
      query->ptr     = aconf;
      query->callback = connect_dns_callback;
      adns_gethost(aconf->host, query);

      aconf->dns_pending = 1;
      return 0;
    }
  }
  cptr = make_client(NULL);
  cptr->dns_query = reply;
  
  /*
   * Copy these in so we have something for error detection.
   */
  strncpy_irc(cptr->name, aconf->name, HOSTLEN);
  strncpy_irc(cptr->host, aconf->host, HOSTLEN);

  if (!connect_inet(aconf, cptr)) {
    if (by && IsPerson(by) && !MyClient(by))
      sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
                 me.name, by->name, cptr);
    free_client(cptr);
    return 0;
  }
  /*
   * NOTE: if we're here we have a valid C:Line and the client should
   * have started the connection and stored the remote address/port and
   * ip address name in itself
   * 
   * Attach config entries to client here rather than in
   * completed_connection. This to avoid null pointer references
   */
  if (!attach_cn_lines(cptr, aconf->name, aconf->host))
    {
      sendto_realops("Server %s is not enabled for connecting: no C/N-line",
                 aconf->name);
      if (by && IsPerson(by) && !MyClient(by))
        sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
                   me.name, by->name, cptr);
      det_confs_butmask(cptr, 0);

      free_client(cptr);
      return 0;
    }
  /* 
   * at this point we have a connection in progress and C/N lines
   * attached to the client, the socket info should be saved in the
   * client and it should either be resolved or have a valid address.
   *
   * The socket has been connected or connect is in progress.
   */
  make_server(cptr);
  if (by && IsPerson(by))
    {
      strcpy(cptr->serv->by, by->name);
      if (cptr->serv->user) 
        free_user(cptr->serv->user, NULL);
      cptr->serv->user = by->user;
      by->user->refcnt++;
    } 
  else
    {
      strcpy(cptr->serv->by, "AutoConn.");
      if (cptr->serv->user)
        free_user(cptr->serv->user, NULL);
      cptr->serv->user = NULL;
    }
  cptr->serv->up = me.name;

  if (cptr->fd > highest_fd)
    highest_fd = cptr->fd;
  local[cptr->fd] = cptr;

  SetConnecting(cptr);

  add_client_to_list(cptr);
  fdlist_add(cptr->fd, FDL_DEFAULT);
  nextping = CurrentTime;

  return 1;
}

/*
 * close_connection
 *        Close the physical connection. This function must make
 *        MyConnect(cptr) == FALSE, and set cptr->from == NULL.
 */
void close_connection(struct Client *cptr)
{
  struct ConfItem *aconf;
  assert(0 != cptr);

  if (IsServer(cptr))
    {
      ServerStats->is_sv++;
      ServerStats->is_sbs += cptr->sendB;
      ServerStats->is_sbr += cptr->receiveB;
      ServerStats->is_sks += cptr->sendK;
      ServerStats->is_skr += cptr->receiveK;
      ServerStats->is_sti += CurrentTime - cptr->firsttime;
      if (ServerStats->is_sbs > 2047)
        {
          ServerStats->is_sks += (ServerStats->is_sbs >> 10);
          ServerStats->is_sbs &= 0x3ff;
        }
      if (ServerStats->is_sbr > 2047)
        {
          ServerStats->is_skr += (ServerStats->is_sbr >> 10);
          ServerStats->is_sbr &= 0x3ff;
        }
      /*
       * If the connection has been up for a long amount of time, schedule
       * a 'quick' reconnect, else reset the next-connect cycle.
       */
      if ((aconf = find_conf_exact(cptr->name, cptr->username,
                                   cptr->host, CONF_CONNECT_SERVER)))
        {
          /*
           * Reschedule a faster reconnect, if this was a automatically
           * connected configuration entry. (Note that if we have had
           * a rehash in between, the status has been changed to
           * CONF_ILLEGAL). But only do this if it was a "good" link.
           */
          aconf->hold = time(NULL);
          aconf->hold += (aconf->hold - cptr->since > HANGONGOODLINK) ?
            HANGONRETRYDELAY : ConfConFreq(aconf);
          if (nextconnect > aconf->hold)
            nextconnect = aconf->hold;
        }

    }
  else if (IsClient(cptr))
    {
      ServerStats->is_cl++;
      ServerStats->is_cbs += cptr->sendB;
      ServerStats->is_cbr += cptr->receiveB;
      ServerStats->is_cks += cptr->sendK;
      ServerStats->is_ckr += cptr->receiveK;
      ServerStats->is_cti += CurrentTime - cptr->firsttime;
      if (ServerStats->is_cbs > 2047)
        {
          ServerStats->is_cks += (ServerStats->is_cbs >> 10);
          ServerStats->is_cbs &= 0x3ff;
        }
      if (ServerStats->is_cbr > 2047)
        {
          ServerStats->is_ckr += (ServerStats->is_cbr >> 10);
          ServerStats->is_cbr &= 0x3ff;
        }
    }
  else
    ServerStats->is_ni++;
  
  if (-1 < cptr->fd) {
    flush_connections(cptr);
    local[cptr->fd] = NULL;
    fdlist_delete(cptr->fd, FDL_ALL);
    close(cptr->fd);
    cptr->fd = -1;
  }

#ifdef ZIP_LINKS
    /*
     * the connection might have zip data (even if
     * FLAGS2_ZIP is not set)
     */
  if (IsServer(cptr))
    zip_free(cptr);
#endif
  DBufClear(&cptr->sendQ);
  DBufClear(&cptr->recvQ);
  memset(cptr->passwd, 0, sizeof(cptr->passwd));
  /*
   * clean up extra sockets from P-lines which have been discarded.
   */
  if (cptr->listener) {
    assert(0 < cptr->listener->ref_count);
    if (0 == --cptr->listener->ref_count && !cptr->listener->active) 
      close_listener(cptr->listener);
    cptr->listener = 0;
  }

  for (; highest_fd > 0; --highest_fd) {
    if (local[highest_fd])
      break;
  }

  det_confs_butmask(cptr, 0);
  cptr->from = NULL; /* ...this should catch them! >:) --msa */
}

/*
 * add_connection - creates a client which has just connected to us on 
 * the given fd. The sockhost field is initialized with the ip# of the host.
 * The client is sent to the auth module for verification, and not put in
 * any client list yet.
 */
void add_connection(struct Listener* listener, int fd)
{
  struct Client*           new_client;
  struct sockaddr_in addr;
  int                len = sizeof(struct sockaddr_in);

  assert(0 != listener);

  /* 
   * get the client socket name from the socket
   * the client has already been checked out in accept_connection
   */
  if (getpeername(fd, (struct sockaddr*) &addr, &len)) {
    report_error("Failed in adding new connection %s :%s", 
                 get_listener_name(listener), errno);
    ServerStats->is_ref++;
    close(fd);
    return;
  }

  new_client = make_client(NULL);

  /* 
   * copy address to 'sockhost' as a string, copy it to host too
   * so we have something valid to put into error messages...
   */
  strncpy_irc(new_client->sockhost, 
              inetntoa((char*) &addr.sin_addr), HOSTIPLEN);
  strcpy(new_client->host, new_client->sockhost);
  new_client->ip.s_addr = addr.sin_addr.s_addr;
  new_client->port      = ntohs(addr.sin_port);
  new_client->fd        = fd;

  new_client->listener  = listener;
  ++listener->ref_count;

#ifdef HIDE_SERVERS_IPS
  if (!set_non_blocking(new_client->fd))
    report_error(NONB_ERROR_MSG, get_client_name(new_client, MASK_IP), errno);
  if (!disable_sock_options(new_client->fd))
    report_error(OPT_ERROR_MSG, get_client_name(new_client, MASK_IP), errno);
#else    
  if (!set_non_blocking(new_client->fd))
    report_error(NONB_ERROR_MSG, get_client_name(new_client, TRUE), errno);
  if (!disable_sock_options(new_client->fd))
    report_error(OPT_ERROR_MSG, get_client_name(new_client, TRUE), errno);
#endif    
  start_auth(new_client);
}

/*
 * parse_client_queued - parse client queued messages
 */
static int parse_client_queued(struct Client* cptr)
{
  int dolen  = 0;

  while (DBufLength(&cptr->recvQ) && !NoNewLine(cptr) &&
         ((cptr->status < STAT_UNKNOWN) || (cptr->since - CurrentTime < 10))) {
    /*
     * If it has become registered as a Server
     * then skip the per-message parsing below.
     */
    if (IsServer(cptr)) {
      /* 
       * This is actually useful, but it needs the ZIP_FIRST
       * kludge or it will break zipped links  -orabidoo
       */
      dolen = dbuf_get(&cptr->recvQ, readBuf, READBUF_SIZE);

      if (0 == dolen)
        break;
      return dopacket(cptr, readBuf, dolen);
    }
    dolen = dbuf_getmsg(&cptr->recvQ, readBuf, READBUF_SIZE);
    /*
     * Devious looking...whats it do ? well..if a client
     * sends a *long* message without any CR or LF, then
     * dbuf_getmsg fails and we pull it out using this
     * loop which just gets the next 512 bytes and then
     * deletes the rest of the buffer contents.
     * -avalon
     */
    if (0 == dolen) {
      if (DBufLength(&cptr->recvQ) < 510) {
        cptr->flags |= FLAGS_NONL;
        break;
      }
      DBufClear(&cptr->recvQ);
      break;
    }
    else if (CLIENT_EXITED == client_dopacket(cptr, readBuf, dolen))
      return CLIENT_EXITED;
  }
  return 1;
}

/*
 * read_packet - Read a 'packet' of data from a connection and process it.
 * Do some tricky stuff for client connections to make sure they don't do
 * any flooding >:-) -avalon
 */
#define SBSD_MAX_CLIENT 6090

static int read_packet(struct Client *cptr)
{
  int length = 0;
  int done = 0;
  char *rb;
  if (!(IsPerson(cptr) && DBufLength(&cptr->recvQ) > SBSD_MAX_CLIENT)) {
    errno = 0;
    length = recv(cptr->fd, readBuf, READBUF_SIZE, 0);
    /*
     * If not ready, fake it so it isnt closed
     */
    if (length == -1) {
      if (EWOULDBLOCK == errno || EAGAIN == errno)
        length = 1;
      return length;
    }
  }
  if (length == 0)
    return length;

#ifdef REJECT_HOLD
  /* 
   * If client has been marked as rejected i.e. it is a client
   * that is trying to connect again after a k-line,
   * pretend to read it but don't actually.
   * -Dianora
   *
   * FLAGS_REJECT_HOLD should NEVER be set for non local client 
   */
  if (IsRejectHeld(cptr))
    return 1;
#endif

  cptr->lasttime = CurrentTime;
  if (cptr->lasttime > cptr->since)
    cptr->since = cptr->lasttime;
  cptr->flags &= ~(FLAGS_PINGSENT | FLAGS_NONL);

  /*
   * For server connections, we process as many as we can without
   * worrying about the time of day or anything :)
   */
  rb = readBuf;

  if (PARSE_AS_SERVER(cptr)) {
    if (length > 0) {
      if ((done = dopacket(cptr, rb, length)))
	return done;
    }
  }
  else {
    /*
     * Before we even think of parsing what we just read, stick
     * it on the end of the receive queue and do it when its
     * turn comes around.
     */
    if (!dbuf_put(&cptr->recvQ, rb, length))
      return exit_client(cptr, cptr, cptr, "dbuf_put fail");

    if (IsPerson(cptr) &&
#ifdef NO_OPER_FLOOD
        !IsEmpowered(cptr) &&
#endif
        DBufLength(&cptr->recvQ) > CLIENT_FLOOD) {
      return exit_client(cptr, cptr, cptr, "Excess Flood");
    }
    return parse_client_queued(cptr);
  }
  return 1;
}

static void error_exit_client(struct Client* cptr, int error)
{
  /*
   * ...hmm, with non-blocking sockets we might get
   * here from quite valid reasons, although.. why
   * would select report "data available" when there
   * wasn't... so, this must be an error anyway...  --msa
   * actually, EOF occurs when read() returns 0 and
   * in due course, select() returns that fd as ready
   * for reading even though it ends up being an EOF. -avalon
   */
  char errmsg[255];
  int  current_error = get_sockerr(cptr->fd);

  Debug((DEBUG_ERROR, "READ ERROR: fd = %d %d %d",
         cptr->fd, current_error, error));
  if (IsServer(cptr) || IsHandshake(cptr))
    {
      int connected = CurrentTime - cptr->firsttime;

#ifdef HIDE_SERVERS_IPS
      if (0 == error)
        sendto_ops("Server %s closed the connection",
		   get_client_name(cptr, MASK_IP));
      else
        report_error("Lost connection to %s: %s",
		   get_client_name(cptr, MASK_IP), current_error);
#else		   
      if (0 == error)
        sendto_ops("Server %s closed the connection",
                   get_client_name(cptr, TRUE));
      else
        report_error("Lost connection to %s: %s", 
                     get_client_name(cptr, TRUE), current_error);
#endif		     
      sendto_realops("%s had been connected for %d day%s, %2d:%02d:%02d",
                 cptr->name, connected/86400,
                 (connected/86400 == 1) ? "" : "s",
                 (connected % 86400) / 3600, (connected % 3600) / 60,
                 connected % 60);
    }
  if (0 == error)
    strcpy(errmsg, "Remote closed the connection");
  else
    ircsprintf(errmsg, "Read error: %d (%s)", 
               current_error, strerror(current_error));
  exit_client(cptr, cptr, &me, errmsg);
}

/*
 * Check all connections for new connections and input data that is to be
 * processed. Also check for connections with data queued and whether we can
 * write it out.
 */
#ifndef USE_POLL
int read_message(time_t delay, unsigned char mask)        /* mika */

     /* Don't ever use ZERO here, unless you mean to poll
        and then you have to have sleep/wait somewhere 
        else in the code.--msa
      */
{
  struct Client*      cptr;
  int                 nfds;
  struct timeval      wait;
  time_t              delay2 = delay;
  time_t              now;
  u_long              usec = 0;
  int                 res;
  int                 length;
  struct AuthRequest* auth = 0;
  struct AuthRequest* auth_next = 0;
  struct Listener*    listener = 0;
  int                 i;

  now = CurrentTime;

  for (res = 0;;)
    {
      FD_ZERO(read_set);
      FD_ZERO(write_set);

      for (auth = AuthPollList; auth; auth = auth->next) {
        assert(-1 < auth->fd);
        if (IsAuthConnect(auth))
          FD_SET(auth->fd, write_set);
        else /* if(IsAuthPending(auth)) */
          FD_SET(auth->fd, read_set);
      }
      for (listener = ListenerPollList; listener; listener = listener->next) {
        assert(-1 < listener->fd);
        FD_SET(listener->fd, read_set);
      }
      for (i = 0; i <= highest_fd; i++)
        {
          if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
            continue;

          /*
           * anything that IsMe should NEVER be in the local client array
           */
          assert(!IsMe(cptr));

          if (DBufLength(&cptr->recvQ) && delay2 > 2)
            delay2 = 1;
          if (DBufLength(&cptr->recvQ) < 4088)        
            {
               FD_SET(i, read_set);
            }
          else parse_client_queued(cptr);
		/* bubye annoying bug. *squish* -gnp */

          if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
              || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
              )
            {
               FD_SET(i, write_set);
            }
        }
      
/*      if (ResolverFileDescriptor >= 0)
        {
          FD_SET(ResolverFileDescriptor, read_set);
        }
*/
      wait.tv_sec = 0;
      wait.tv_usec = 250000;

      nfds = select(MAXCONNECTIONS, read_set, write_set, 0, &wait);

      if ((CurrentTime = time(NULL)) == -1)
        {
          Log("Clock Failure");
          restart("Clock failure");
        }   

      if (nfds == -1 && errno == EINTR)
        {
          return -1;
        }
      else if( nfds >= 0)
        break;

      res++;
      if (res > 5)
        restart("too many select errors");
      sleep(10);
    }

  /*
   * Check the name resolver
   */
/*
  if (-1 < ResolverFileDescriptor && 
      FD_ISSET(ResolverFileDescriptor, read_set)) {
    get_res();
    --nfds;
  }
*/
  /*
   * Check the auth fd's
   */
  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    assert(-1 < auth->fd);
    if (IsAuthConnect(auth) && FD_ISSET(auth->fd, write_set)) {
      send_auth_query(auth);
      if (0 == --nfds)
        break;
    }
    else if (FD_ISSET(auth->fd, read_set)) {
      read_auth_reply(auth);
      if (0 == --nfds)
        break;
    }
  }
  for (listener = ListenerPollList; listener; listener = listener->next) {
    assert(-1 < listener->fd);
    if (FD_ISSET(listener->fd, read_set))
      accept_connection(listener);
  }

  for (i = 0; i <= highest_fd; i++) {
    if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
      continue;

    /*
     * See if we can write...
     */
    if (FD_ISSET(i, write_set)) {
      --nfds;
      if (IsConnecting(cptr)) {
        if (!completed_connection(cptr)) {
          exit_client(cptr, cptr, &me, "Lost C/N Line");
          continue;
        }
        send_queued(cptr);
          if (!IsDead(cptr))
            continue;
      }
      else {
        /*
         * ...room for writing, empty some queue then...
         */
        send_queued(cptr);
        if (!IsDead(cptr))
          continue;
      }
      exit_client(cptr, cptr, &me, 
                 (cptr->flags & FLAGS_SENDQEX) ? 
                 "SendQ Exceeded" : strerror(get_sockerr(cptr->fd)));
      continue;
    }
    length = 1;     /* for fall through case */

    if (FD_ISSET(i, read_set)) {
      --nfds;
      length = read_packet(cptr);
    }
    else if (PARSE_AS_CLIENT(cptr) && !NoNewLine(cptr))
      length = parse_client_queued(cptr);

    if (length > 0 || length == CLIENT_EXITED)
      continue;
    if (IsDead(cptr)) {
       exit_client(cptr, cptr, &me,
                    strerror(get_sockerr(cptr->fd)));
       continue;
    }
    error_exit_client(cptr, length);
    errno = 0;
  }
  return 0;
}
  
#else /* USE_POLL */

#if defined(POLLMSG) && defined(POLLIN) && defined(POLLRDNORM)
#define POLLREADFLAGS (POLLMSG | POLLIN | POLLRDNORM)
#else

# if defined(POLLIN) && defined(POLLRDNORM)
# define POLLREADFLAGS (POLLIN | POLLRDNORM)
# else

#  if defined(POLLIN)
#  define POLLREADFLAGS POLLIN
#  else

#   if defined(POLLRDNORM)
#    define POLLREADFLAGS POLLRDNORM
#   endif

#  endif

# endif

#endif

#if defined(POLLOUT) && defined(POLLWRNORM)
#define POLLWRITEFLAGS (POLLOUT | POLLWRNORM)
#else

# if defined(POLLOUT)
# define POLLWRITEFLAGS POLLOUT
# else

#  if defined(POLLWRNORM)
#  define POLLWRITEFLAGS POLLWRNORM
#  endif

# endif

#endif

#if defined(POLLERR) && defined(POLLHUP)
#define POLLERRORS (POLLERR | POLLHUP)
#else
#define POLLERRORS POLLERR
#endif

#define PFD_SETR(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLREADFLAGS; } while (0)
#define PFD_SETW(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLWRITEFLAGS; } while (0)
#define CHECK_PFD(thisfd)                     \
        if (pfd->fd != thisfd) {              \
                pfd = &poll_fdarray[nbr_pfds++];\
                poll_fdarray[nbr_pfds].fd = -1; \
                pfd->fd     = thisfd;           \
                pfd->events = 0;                \
        }

int read_message(time_t delay, unsigned char mask)
{
  struct Client*       cptr;
  int                  nfds;
  struct timeval       wait;

  static struct pollfd poll_fdarray[MAXCONNECTIONS];
  struct pollfd*       pfd = poll_fdarray;
  int                  nbr_pfds = 0;
  time_t               delay2 = delay;
  u_long               usec = 0;
  int                  res = 0;
  int                  length;
  int                  fd;
  struct AuthRequest*  auth;
  struct AuthRequest*  auth_next;
  struct Listener*     listener;
  int                  rr;
  int                  rw;
  int                  i;

  for ( ; ; ) {
    nbr_pfds = 0;
    pfd      = poll_fdarray;
    pfd->fd  = -1;
    auth = 0;

    /*
     * set auth descriptors
     */
    for (auth = AuthPollList; auth; auth = auth->next) {
      assert(-1 < auth->fd);
      auth->index = nbr_pfds;
      if (IsAuthConnect(auth))
        PFD_SETW(auth->fd);
      else
        PFD_SETR(auth->fd);
    }
    /*
     * set listener descriptors
     */
    for (listener = ListenerPollList; listener; listener = listener->next) {
      assert(-1 < listener->fd);
#ifdef CONNECTFAST
      listener->index = nbr_pfds;
      PFD_SETR(listener->fd);
#else
     /* 
      * It is VERY bad if someone tries to send a lot
      * of clones to the server though, as mbuf's can't
      * be allocated quickly enough... - Comstud
      */
      listener->index = -1;
      if (CurrentTime > (listener->last_accept + 2)) {
        listener->index = nbr_pfds;
        PFD_SETR(listener->fd);
      }
      else if (delay2 > 2)
        delay2 = 2;
#endif
    }
    /*
     * set client descriptors
     */
    for (i = 0; i <= highest_fd; ++i) {
      if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
        continue;

     /*
      * anything that IsMe should NEVER be in the local client array
      */
      assert(!IsMe(cptr));
      if (DBufLength(&cptr->recvQ) && delay2 > 2)
        delay2 = 1;

      if (DBufLength(&cptr->recvQ) < 4088)
        PFD_SETR(i);
      else parse_client_queued(cptr);
      /* you go squish now. -gnp */
      
      if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
          || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
          )
        PFD_SETW(i);
    }

    wait.tv_sec = IRCD_MIN(delay2, delay);
    wait.tv_usec = usec;
    nfds = poll(poll_fdarray, nbr_pfds, 250);
    if ((CurrentTime = time(0)) == -1)
      {
        Log("Clock Failure");
        restart("Clock failed");
      }   
    if (nfds == -1 && ((errno == EINTR) || (errno == EAGAIN)))
      return -1;
    else if (nfds >= 0)
      break;
    report_error("poll %s:%s", me.name, errno);
    res++;
    if (res > 5)
      restart("too many poll errors");
    sleep(10);
  }
  /*
   * check auth descriptors
   */
  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    i = auth->index;
    /*
     * check for any event, we only ask for one at a time
     */
    if (poll_fdarray[i].revents) { 
      if (IsAuthConnect(auth))
        send_auth_query(auth);
      else
        read_auth_reply(auth);
      if (0 == --nfds)
        break;
    }
  }
  /*
   * check listeners
   */
  for (listener = ListenerPollList; listener; listener = listener->next) {
    if (-1 == listener->index)
      continue;
    i = listener->index;
    if (poll_fdarray[i].revents) {
      accept_connection(listener);
      if (0 == --nfds)
        break;
    }
  }
  /*
   * i contains the next non-auth/non-listener index, since we put the 
   * resolver, auth and listener, file descriptors in poll_fdarray first, 
   * the very next one should be the start of the clients
   */
  pfd = &poll_fdarray[++i];
    
  for ( ; (i < nbr_pfds); i++, pfd++)
    {
      fd = pfd->fd;                   
      rr = pfd->revents & POLLREADFLAGS;
      rw = pfd->revents & POLLWRITEFLAGS;
      if (pfd->revents & POLLERRORS)
        {
          if (pfd->events & POLLREADFLAGS)
            rr++;
          if (pfd->events & POLLWRITEFLAGS)
            rw++;
        }
      if (!(cptr = local[fd]))
        continue;

      if (rw)
        {
          if (IsConnecting(cptr)) {
            if (!completed_connection(cptr)) {
              exit_client(cptr, cptr, &me, "Lost C/N Line");
              continue;
            }
            send_queued(cptr);
            if (!IsDead(cptr))
              continue;
          }
          else {
            /*
             * ...room for writing, empty some queue then...
             */
            send_queued(cptr);
            if (!IsDead(cptr))
              continue;
          }
          exit_client(cptr, cptr, &me, 
                     (cptr->flags & FLAGS_SENDQEX) ? 
                     "SendQ Exceeded" : strerror(get_sockerr(cptr->fd)));
          continue;
        }
      length = 1;     /* for fall through case */
      if (rr)
        length = read_packet(cptr);
      else if (PARSE_AS_CLIENT(cptr) && !NoNewLine(cptr))
        length = parse_client_queued(cptr);

      if (length > 0 || length == CLIENT_EXITED)
        continue;
      if (IsDead(cptr)) {
         exit_client(cptr, cptr, &me,
                      strerror(get_sockerr(cptr->fd)));
         continue;
      }
      error_exit_client(cptr, length);
      errno = 0;
    }
  return 0;
}

#endif /* USE_POLL */

