/*
 * $Id: res.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $ 
 * New res.h
 * Aaron Sethman <androsyn@ratbox.org>
 */

#ifndef _RES_H_INCLUDED
#define _RES_H_INCLUDED 1

#include "config.h"
#include "ircd_defs.h"
/* I hate this *blah* db */
#include "fileio.h"
#include "../adns/adns.h"

#define DNS_BLOCK_SIZE 64

struct DNSQuery {
	void *ptr;
	adns_query query;
	adns_answer answer;
	void (*callback)(void* vptr, adns_answer *reply);
};

void init_resolver(void);
void restart_resolver(void);
void timeout_adns (void);
void dns_writeable (int fd , void *ptr );
void dns_readable (int fd , void *ptr );
void dns_do_callbacks(void);
void dns_select (void);
void adns_gethost (const char *name, struct DNSQuery *req );
void adns_getaddr (struct in_addr *addr , struct DNSQuery *req );
void delete_adns_queries(struct DNSQuery *q);
void do_adns_io();
#endif
