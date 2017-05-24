/*
 * $Id: adns.c,v 1.2 2007/02/14 10:00:42 malign Exp $
 * adns.c  functions to enter libadns 
 *
 * Written by Aaron Sethman <androsyn@ratbox.org>
 */
#include <stdlib.h>
#include "fileio.h"
#include "res.h"
#include "send.h"
#include "s_conf.h"
#include "s_bsd.h"
#include "s_log.h"
#include "client.h"
#include "ircd_defs.h"
#include "numeric.h"
#include "irc_string.h"
#include <errno.h>
#include "../adns/internal.h"
#define ADNS_MAXFD 2

adns_state dns_state;

static struct timeval SystemTime;
/* void delete_adns_queries(struct DNSQuery *q)
 * Input: A pointer to the applicable DNSQuery structure.
 * Output: None
 * Side effects: Cancels a DNS query.
 */
void delete_adns_queries(struct DNSQuery *q)
{
 if (q != NULL && q->query != NULL)
  adns_cancel(q->query);
}                       


/* void restart_resolver(void)
 * Input: None
 * Output: None
 * Side effects: Tears down any old ADNS sockets..reloads the conf
 */
void restart_resolver(void)
{
   adns__rereadconfig(dns_state);
}

/* void init_resolver(void)
 * Input: None
 * Output: None
 * Side effects: Reads the ADNS configuration and sets up the ADNS server
 *               polling and query timeouts.
 */
void init_resolver(void)
{
 int r;
  
 gettimeofday(&SystemTime, NULL);
 r =adns_init(&dns_state, adns_if_noautosys, 0);    
 if(dns_state == NULL) {
   Log("Error opening /etc/resolv.conf: %s; r = %d", strerror(errno), r);
   exit(76);
 }
}

/* void timeout_adns(void *ptr);
 * Input: None used.
 * Output: None
 * Side effects: Cancel any old(expired) DNS queries.
 * Note: Called by the event code.
 */
void timeout_adns()
{
 gettimeofday(&SystemTime, NULL);
 adns_processtimeouts(dns_state, &SystemTime); 
}

#if 0
/* void dns_writeable(int fd, void *ptr)
 * Input: An fd which has become writeable, ptr not used.
 * Output: None.
 * Side effects: Write any queued buffers out.
 * Note: Called by the fd system.
 */
void dns_writeable(int fd, void *ptr)
{
 gettimeofday(&SystemTime, NULL);
 adns_processwriteable(dns_state, fd, &SystemTime); 
 dns_select();
}
#endif
/* void dns_do_callbacks(int fd, void *ptr)
 * Input: None.
 * Output: None.
 * Side effects: Call all the callbacks(into the ircd core) for the
 *               results of a DNS resolution.
 */
void dns_do_callbacks(void)
{
 adns_query q, r;
 adns_answer *answer;
 struct DNSQuery *query;
 adns_forallqueries_begin(dns_state);
 while((q = adns_forallqueries_next(dns_state, (void **)&r)) != NULL)
 {
  switch(adns_check(dns_state, &q, &answer, (void **)&query))  {
   case 0:
    /* Looks like we got a winner */            
    assert(query->callback != NULL);
    query->query = NULL;
    query->callback(query->ptr, answer);
    MyFree(query);
    break;
   case EAGAIN:
    /* Go into the queue again */
    break;
   default:
    assert(query->callback != NULL);
    /* Awww we failed, what a shame */
    query->query = NULL;
    query->callback(query->ptr, NULL);      
    MyFree(query);
    break;
  } 
 }
}


#ifndef USE_POLL
static void do_adns_select(void)
{
	struct timeval tvbuf;
	int maxfd = 2;
	fd_set readfds, writefds, exceptfds;

	tvbuf.tv_sec = 0;
	tvbuf.tv_usec = 0;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	adns_beforeselect(dns_state, &maxfd, &readfds, &writefds, &exceptfds, 0, &tvbuf, 0);
	select(maxfd, &readfds, &writefds, &exceptfds, &tvbuf);
	adns_afterselect(dns_state, maxfd, &readfds, &writefds, &exceptfds, &tvbuf); 
}
#else
static void do_adns_poll(void)
{
	struct pollfd pfd[MAXFD_POLL];
	int nfds = MAXFD_POLL;
	int timeout = 0;
	
	adns_beforepoll(dns_state, pfd, &nfds, &timeout, &SystemTime);
#ifdef KQUEUE
        kqueue(pfd, nfds, timeout);
#else
	poll(pfd, nfds, timeout);
#endif
	adns_afterpoll(dns_state, pfd, nfds, &SystemTime);
}

#endif

void do_adns_io(void)
{
        gettimeofday(&SystemTime, NULL);
#ifdef USE_POLL
	do_adns_poll();
#else
	do_adns_select();
#endif
	adns_processany(dns_state);
	dns_do_callbacks();
	timeout_adns();
}
/* void adns_gethost(const char *name, struct DNSQuery *req);
 * Input: A name, an address family, a DNSQuery structure.
 * Output: None
 * Side effects: Sets up a query structure and sends off a DNS query to
 *               the DNS server to resolve an "A"(address) entry by name.
 */
void adns_gethost(const char *name, struct DNSQuery *req)
{
 assert(dns_state->nservers > 0);
 adns_submit(dns_state, name, adns_r_addr, adns_qf_owner, req,
             &req->query);

}

/* void adns_getaddr(struct irc_inaddr *addr, int aftype,
                     struct DNSQuery *req);
 * Input: An address, an address family, a DNSQuery structure.
 * Output: None
 * Side effects: Sets up a query entry and sends it to the DNS server to
 *               resolve an IP address to a domain name.
 */
void adns_getaddr(struct in_addr *addr,
                  struct DNSQuery *req)
{
 struct sockaddr_in ipn;
 assert(dns_state->nservers > 0);
  ipn.sin_family = AF_INET;
  ipn.sin_port = 0;
  ipn.sin_addr.s_addr = addr->s_addr;
  adns_submit_reverse(dns_state, (struct sockaddr *)&ipn,
                      adns_r_ptr, adns_qf_owner|adns_qf_cname_loose|adns_qf_quoteok_anshost, req, &req->query);
}

int ctype_whitespace(int c)
 { return c==' ' || c=='\n' || c=='\t'; }

int ctype_digit(int c)
 { return c>='0' && c<='9'; }

int ctype_alpha(int c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int ctype_822special(int c)
{ return strchr("()<>@,;:\\\".[]",c) != 0; }

int ctype_domainunquoted(int c)
{
  return ctype_alpha(c) || ctype_digit(c) || (strchr("-_/+",c) != 0);
}

int errno_resources(int e)
{ return e==ENOMEM || e==ENOBUFS; }
