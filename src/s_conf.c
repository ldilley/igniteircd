/************************************************************************
 *   IRC - Internet Relay Chat, src/s_conf.c
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
 *  (C) 1988 University of Oulu,Computing Center and Jarkko Oikarinen"
 *
 *  $Id: s_conf.c,v 1.1.1.1 2006/03/08 23:28:12 malign Exp $
 */
#include "m_commands.h"
#include "s_conf.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "res.h"    /* gethost_byname, gethost_byaddr */
#include "s_bsd.h"
#include "s_log.h"
#include "send.h"
#include "struct.h"
#include "s_debug.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <assert.h>


extern ConfigFileEntryType ConfigFileEntry; /* defined in ircd.c */

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

struct sockaddr_in vserv;
int                specific_virtual_host = 0;

/* internally defined functions */

static void lookup_confhost(struct ConfItem* aconf);
static void do_include_conf(void);
static int  SplitUserHost( struct ConfItem * );
static char *getfield(char *newline);

static FBFILE*  openconf(const char* filename);
static void     initconf(FBFILE*, int);
static void     clear_out_old_conf(void);
static void     flush_deleted_I_P(void);

static  int     attach_iline(struct Client *, struct ConfItem *, const char *);
static	int	verify_access(struct Client *, const char *, char **);

struct ConfItem *find_special_conf(char *, int );

static void add_q_line(struct ConfItem *);
static void clear_q_lines(void);
static void clear_special_conf(struct ConfItem **);
static struct ConfItem* find_tkline(const char*, const char*, unsigned long);
static void expire_temp_klines(struct ConfItem **tklines);

struct ConfItem *temporary_klines = NULL;
struct ConfItem *temporary_ip_klines = NULL;

static  char *set_conf_flags(struct ConfItem *,char *);
static  int  oper_privs_from_string(int,char *);
static  int  oper_flags_from_string(char *);

/* externally defined functions */
extern  void    outofmemory(void);        /* defined in list.c */

#ifdef GLINES
extern  struct ConfItem *find_gkill(aClient *,char *); /* defined in m_gline.c */
#endif

/* usually, with hash tables, you use a prime number...
 * but in this case I am dealing with ip addresses, not ascii strings.
 */

#define IP_HASH_SIZE 0x1000

typedef struct ip_entry
{
  unsigned long ip;
  int        count;
  struct ip_entry *next;
#ifdef LIMIT_UH
  Link  *ptr_clients_on_this_ip;
  int        count_of_idented_users_on_this_ip;
#endif
}IP_ENTRY;

static IP_ENTRY *ip_hash_table[IP_HASH_SIZE];

static int hash_ip(unsigned long);

static IP_ENTRY *find_or_add_ip(aClient *,const char *);

#ifdef LIMIT_UH
static int count_users_on_this_ip(IP_ENTRY *,aClient *,const char *);
#endif

/* general conf items link list root */
struct ConfItem* ConfigItemList = NULL;

/* conf xline link list root */
struct ConfItem        *x_conf = ((struct ConfItem *)NULL);


static void makeQlineEntry(aQlineItem *, struct ConfItem *, char *);

/* conf qline link list root */
aQlineItem         *q_conf = ((aQlineItem *)NULL);

/* conf uline link list root */
struct ConfItem        *u_conf = ((struct ConfItem *)NULL);

/* keep track of .include files to hash in */
struct ConfItem        *include_list = ((struct ConfItem *)NULL);

/*
 * conf_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * if successful save hp in the conf item it was called with
 */
static void conf_dns_callback(void* vptr, adns_answer *reply)
{
  struct ConfItem* aconf = (struct ConfItem*) vptr;
  aconf->dns_pending = 0;
  if (reply->status == adns_s_ok)
    aconf->ipnum.s_addr = reply->rrs.addr->addr.inet.sin_addr.s_addr;
  MyFree(reply);
}

/*
 * conf_dns_lookup - do a nameserver lookup of the conf host
 * if the conf entry is currently doing a ns lookup do nothing, otherwise
 * if the lookup returns a null pointer, set the conf dns_pending flag
 */
void conf_dns_lookup(struct ConfItem* aconf)
{
  if (!aconf->dns_pending)
  {
    struct DNSQuery *query = MyMalloc(sizeof(struct DNSQuery));
    query->ptr     = aconf;
    query->callback = conf_dns_callback;
    adns_gethost(aconf->host, query);
    aconf->dns_pending = 1;
  }
}

/*
 * make_conf - create a new conf entry
 */
struct ConfItem* make_conf()
{
  struct ConfItem* aconf;

  aconf = (struct ConfItem*) MyMalloc(sizeof(struct ConfItem));
  memset(aconf, 0, sizeof(struct ConfItem));
  aconf->status       = CONF_ILLEGAL;
  aconf->ipnum.s_addr = INADDR_NONE;

#if defined(NULL_POINTER_NOT_ZERO)
  aconf->next = NULL;
  aconf->host = NULL;
  aconf->passwd = NULL;
  aconf->name = NULL;
  ClassPtr(aconf) = NULL;
#endif
  return (aconf);
}

/*
 * delist_conf - remove conf item from ConfigItemList
 */
static void 
delist_conf(struct ConfItem* aconf)
{
  if (aconf == ConfigItemList)
    ConfigItemList = ConfigItemList->next;
  else
    {
      struct ConfItem* bconf;

      for (bconf = ConfigItemList; aconf != bconf->next; bconf = bconf->next)
        ;
      bconf->next = aconf->next;
    }
  aconf->next = NULL;
}

void 
free_conf(struct ConfItem* aconf)
{
  assert(aconf != NULL);

  if (aconf->dns_pending)
    delete_adns_queries(aconf->dns_query);
  MyFree(aconf->host);
  if (aconf->passwd)
    memset(aconf->passwd, 0, strlen(aconf->passwd));
  MyFree(aconf->passwd);
  MyFree(aconf->user);
  MyFree(aconf->name);
  MyFree((char*) aconf);
}

/*
 * remove all conf entries from the client except those which match
 * the status field mask.
 */
void det_confs_butmask(struct Client* cptr, int mask)
{
  struct SLink* conf_link;
  struct SLink* conf_link_next;

  for (conf_link = cptr->confs; conf_link; conf_link = conf_link_next)
    {
      conf_link_next = conf_link->next;
      if ((conf_link->value.aconf->status & mask) == 0)
        detach_conf(cptr, conf_link->value.aconf);
    }
}

/*
 * rewrote to use a struct -Dianora
 */
static struct LinkReport {
  int conf_type;
  int rpl_stats;
  int conf_char;
} report_array[] = {
  { CONF_CONNECT_SERVER,   RPL_STATSCLINE, 'C'},
  { CONF_NOCONNECT_SERVER, RPL_STATSNLINE, 'N'},
  { CONF_LEAF,             RPL_STATSLLINE, 'L'},
  { CONF_ADMINISTRATOR,    RPL_STATSOLINE, 'O'},
  { CONF_HUB,              RPL_STATSHLINE, 'H'},
  { CONF_OPER,             RPL_STATSOLINE, 'o'},
  { 0, 0, '\0' }
};

void report_configured_links(struct Client* sptr, int mask)
{
  struct ConfItem*   tmp;
  struct LinkReport* p;
  char*              host;
  char*              pass;
  char*              user;
  char*              name;
  int                port;

  for (tmp = ConfigItemList; tmp; tmp = tmp->next) {
    if (tmp->status & mask)
      {
        for (p = &report_array[0]; p->conf_type; p++)
          if (p->conf_type == tmp->status)
            break;
        if(p->conf_type == 0)return;

        get_printable_conf(tmp, &name, &host, &pass, &user, &port);

        if(mask & (CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER))
          {
            char c;

            c = p->conf_char;
            if(tmp->flags & CONF_FLAGS_ZIP_LINK)
              c = 'c';

            /* Don't allow non opers to see actual ips */
#ifndef HIDE_SERVERS_IPS
            if(IsEmpowered(sptr))
              sendto_one(sptr, form_str(p->rpl_stats), me.name,
                         sptr->name, c,
#ifdef SERVERHIDE
                         "255.255.255.255",
#else
                         host,
#endif
                         name,
                         port,
                         get_conf_class(tmp),
                         oper_flags_as_string((int)tmp->hold));
            else
#endif	   
              sendto_one(sptr, form_str(p->rpl_stats), me.name,
                         sptr->name, c,
                         "*@255.255.255.255",
                         name,
                         port,
                         get_conf_class(tmp));

          }
        else if(mask & (CONF_ADMINISTRATOR|CONF_OPER))
          {
            /* Don't allow non opers to see oper privs */
            if(IsEmpowered(sptr))
              sendto_one(sptr, form_str(p->rpl_stats), me.name,
                         sptr->name,
                         p->conf_char,
                         user, host, name,
                         oper_privs_as_string((struct Client *)NULL,port),
                         get_conf_class(tmp),
                         oper_flags_as_string((int)tmp->hold));
            else
              sendto_one(sptr, form_str(p->rpl_stats), me.name,
                         sptr->name,
                         p->conf_char,
                         user,
#ifdef SERVERHIDE
                         IsEmpowered(sptr) ? host : "255.255.255.255",
#else
                         host,
#endif
                         name,
                         "0",
                         get_conf_class(tmp),
                         "");
          }
        else
          sendto_one(sptr, form_str(p->rpl_stats), me.name,
                     sptr->name, p->conf_char,
                     host, name, port,
                     get_conf_class(tmp));
      }
  }
}

/*
 * report_specials - report special conf entries
 *
 * inputs       - struct Client pointer to client to report to
 *              - int flags type of special struct ConfItem to report
 *              - int numeric for struct ConfItem to report
 * output       - none
 * side effects -
 */
void report_specials(struct Client* sptr, int flags, int numeric)
{
  struct ConfItem* this_conf;
  struct ConfItem* aconf;
  char*            name;
  char*            host;
  char*            pass;
  char*            user;
  int              port;

  if (flags & CONF_XLINE)
    this_conf = x_conf;
  else if (flags & CONF_ULINE)
    this_conf = u_conf;
  else return;

  for (aconf = this_conf; aconf; aconf = aconf->next)
    if (aconf->status & flags)
      {
        get_printable_conf(aconf, &name, &host, &pass, &user, &port);

        sendto_one(sptr, form_str(numeric),
                   me.name,
                   sptr->name,
                   name,
                   pass);
      }
}

/*
 * Ordinary client access check. Look for conf lines which have the same
 * status as the flags passed.
 *
 * outputs
 *  0 = Success
 * -1 = Access denied (no I line match)
 * -2 = Bad socket.
 * -3 = I-line is full
 * -4 = Too many connections from hostname
 * -5 = K-lined
 * also updates reason if a K-line
 *
 */
int check_client(struct Client *cptr, char *username, char **reason)
{
  static char     sockname[HOSTLEN + 1];
  int             i;
 
  ClearAccess(cptr);

  if ((i = verify_access(cptr, username, reason)))
    {
      Log("Access denied: %s[%s]", cptr->name, sockname);
      return i;
    }

  return 0;
}

/*
 * verify_access
 *
 * inputs	- pointer to client to verify
 *		- pointer to proposed username
 * output	- 0 if success -'ve if not
 * side effect	- find the first (best) I line to attach.
 */
static int
verify_access(struct Client *cptr, const char* username, char **preason)
{
  struct ConfItem* aconf;
#ifdef GLINES
  struct ConfItem* gkill_conf;
#endif /* GLINES */
  struct ConfItem* tkline_conf;
  char       non_ident[USERLEN + 1];

  if (IsGotId(cptr))
    {
      aconf = find_matching_mtrie_conf(cptr->host,cptr->username,
                                       ntohl(cptr->ip.s_addr));
      if(aconf && !IsConfElined(aconf))
        {
          if( (tkline_conf = find_tkline(cptr->host, cptr->username, ntohl(cptr->ip.s_addr))) )
            aconf = tkline_conf;
        }
    }
  else
    {
      non_ident[0] = '~';
      strncpy_irc(&non_ident[1],username, USERLEN - 1);
      non_ident[USERLEN] = '\0';
      aconf = find_matching_mtrie_conf(cptr->host,non_ident,
                                       ntohl(cptr->ip.s_addr));
      if(aconf && !IsConfElined(aconf))
        {
          if( (tkline_conf = find_tkline(cptr->host, non_ident, ntohl(cptr->ip.s_addr))) )
            aconf = tkline_conf;
        }
    }

  if(aconf)
    {
      if (aconf->status & CONF_CLIENT)
        {
#ifdef GLINES
          if ( !IsConfElined(aconf) && !IsConfExemptGline(aconf) )
            {
              if (IsGotId(cptr))
                gkill_conf = find_gkill(cptr,cptr->username);
              else
                gkill_conf = find_gkill(cptr,non_ident);
              if (gkill_conf)
                {
                  *preason = gkill_conf->passwd;
                  sendto_one(cptr, ":%s NOTICE %s :*** G-lined",
                             me.name,cptr->name);
                  return ( -5 );
                }
            }
#endif        /* GLINES */

          if(IsConfDoIdentd(aconf))
            SetNeedId(cptr);

          /* Thanks for spoof idea amm */
          if(IsConfDoSpoofIp(aconf))
            {
#ifdef SPOOF_NOTICE
              sendto_realops("%s spoofing: %s(%s) as %s", cptr->name,
                             cptr->host, inetntoa((char*) &cptr->ip), aconf->name);
#endif /* SPOOF_NOTICE */
              /* Added realhost here, so if the client has a spoofed I: Line, their true host is never lost -malign */
              strncpy_irc(cptr->realhost, cptr->host, HOSTLEN);
              strncpy_irc(cptr->host, aconf->name, HOSTLEN);
              SetIPSpoof(cptr);
              SetIPHidden(cptr);
            }

          return(attach_iline(cptr, aconf, username));
        }
      else if(aconf->status & CONF_KILL)
        {
          *preason = aconf->passwd;
          return(-5);
        }
    }

  return -1;        /* -1 on no match *bleh* */
}

/*
 * attach_iline
 *
 * inputs	- client pointer
 *		- conf pointer
 *		- username (only used if LIMIT_UH is defined)
 * output	-
 * side effects	- do actual attach
 */

static int
attach_iline(aClient *cptr, struct ConfItem *aconf,
	     const char *username)
{
  IP_ENTRY *ip_found;

  /* if LIMIT_UH is set, limit clients by idented usernames not by ip */

  ip_found = find_or_add_ip(cptr, username);

  SetIpHash(cptr);
  ip_found->count++;

#ifdef LIMIT_UH
  if (ConfConFreq(aconf) && (ip_found->count_of_idented_users_on_this_ip
                                  > ConfConFreq(aconf)))
    {
      if(!IsConfFlined(aconf))
        return -4; /* Already at maximum allowed ip#'s */
      else
        {
          sendto_one(cptr,
       ":%s NOTICE %s :*** I: line is full, but you have an >I: line!",
                     me.name,cptr->name);
        }
    }
#else
  /* only check it if its non zero */
  if (ConfConFreq(aconf) && ip_found->count > ConfConFreq(aconf))
    {
      if(!IsConfFlined(aconf))
        return -4; /* Already at maximum allowed ip#'s */
      else
        {
          sendto_one(cptr,
       ":%s NOTICE %s :*** I: line is full, but you have an >I: line!",
                     me.name,cptr->name);
        }
    }
#endif

  return (attach_conf(cptr, aconf) );
}

/* link list of free IP_ENTRY's */

static IP_ENTRY *free_ip_entries;

/*
 * clear_ip_hash_table()
 *
 * input                - NONE
 * output                - NONE
 * side effects        - clear the ip hash table
 *
 * stole the link list pre-allocator from list.c
*/

void clear_ip_hash_table()
{
  void *block_IP_ENTRIES;        /* block of IP_ENTRY's */
  IP_ENTRY *new_IP_ENTRY;        /* new IP_ENTRY being made */
  IP_ENTRY *last_IP_ENTRY;        /* last IP_ENTRY in chain */
  int size;
  int n_left_to_allocate = MAXCONNECTIONS;

  /* ok. if the sizeof the struct isn't aligned with that of the
   * smallest guaranteed valid pointer (void *), then align it
   * ya. you could just turn 'size' into a #define. do it. :-)
   *
   * -Dianora
   */

  size = sizeof(IP_ENTRY) + (sizeof(IP_ENTRY) & (sizeof(void*) - 1) );

  block_IP_ENTRIES = (void *)MyMalloc((size * n_left_to_allocate));  

  free_ip_entries = (IP_ENTRY *)block_IP_ENTRIES;
  last_IP_ENTRY = free_ip_entries;

  /* *shudder* pointer arithmetic */
  while(--n_left_to_allocate)
    {
      block_IP_ENTRIES = (void *)((unsigned long)block_IP_ENTRIES + 
                        (unsigned long) size);
      new_IP_ENTRY = (IP_ENTRY *)block_IP_ENTRIES;
      last_IP_ENTRY->next = new_IP_ENTRY;
      new_IP_ENTRY->next = (IP_ENTRY *)NULL;
      last_IP_ENTRY = new_IP_ENTRY;
    }
  memset((void *)ip_hash_table, 0, sizeof(ip_hash_table));
}

/* 
 * find_or_add_ip()
 *
 * inputs       - cptr
 *              - name
 *
 * output       - pointer to an IP_ENTRY element
 * side effects -
 *
 * If the ip # was not found, a new IP_ENTRY is created, and the ip
 * count set to 0.
 */

static IP_ENTRY *
find_or_add_ip(struct Client *cptr, const char *username)
{
  unsigned long ip_in=cptr->ip.s_addr;        
#ifdef LIMIT_UH
  Link *new_link;
#endif

  int hash_index;
  IP_ENTRY *ptr, *newptr;

  newptr = (IP_ENTRY *)NULL;
  for(ptr = ip_hash_table[hash_index = hash_ip(ip_in)]; ptr; ptr = ptr->next )
    {
      if(ptr->ip == ip_in)
        {
#ifdef LIMIT_UH
          new_link = make_link();
          new_link->value.cptr = cptr;
          new_link->next = ptr->ptr_clients_on_this_ip;
          ptr->ptr_clients_on_this_ip = new_link;
          ptr->count_of_idented_users_on_this_ip =
            count_users_on_this_ip(ptr,cptr,username);
#endif
          return(ptr);
        }
    }

  if ( (ptr = ip_hash_table[hash_index]) != (IP_ENTRY *)NULL )
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
        {
          sendto_realops("s_conf.c free_ip_entries was found NULL in find_or_add");
          sendto_realops("Please report to the hybrid team! ircd-hybrid@the-project.org");
          outofmemory();
        }

      newptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = newptr->next;

      newptr->ip = ip_in;
      newptr->count = 0;
      newptr->next = ptr;
#ifdef LIMIT_UH
      newptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      newptr->ptr_clients_on_this_ip = new_link;
#endif
      return(newptr);
    }
  else
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
        {
          sendto_realops("s_conf.c free_ip_entries was found NULL in find_or_add");
          sendto_realops("Please report to the hybrid team! ircd-hybrid@the-project.org");
          outofmemory();
        }

      ptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = ptr->next;
      ptr->ip = ip_in;
      ptr->count = 0;
      ptr->next = (IP_ENTRY *)NULL;
#ifdef LIMIT_UH
      ptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      ptr->ptr_clients_on_this_ip = new_link;
#endif
     return (ptr);
    }
}

#ifdef LIMIT_UH
static int count_users_on_this_ip(IP_ENTRY *ip_list,
                           aClient *this_client,const char *username)
{
  int count=0;
  Link *ptr;
  
  for( ptr = ip_list->ptr_clients_on_this_ip; ptr; ptr = ptr->next )
    {
      if(ptr->value.cptr->user)
        {
          if (IsGotId(this_client))
            {
              if(!irccmp(ptr->value.cptr->username,username))
                  count++;
            }
          else
            {
              if(this_client == ptr->value.cptr)
                count++;
              else
                if(ptr->value.cptr->username[0] == '~')
                  count++;
            }
        }
    }
  return(count);
}
#endif

/* 
 * remove_one_ip
 *
 * inputs        - unsigned long IP address value
 * output        - NONE
 * side effects  - ip address listed, is looked up in ip hash table
 *                 and number of ip#'s for that ip decremented.
 *                 if ip # count reaches 0, the IP_ENTRY is returned
 *                 to the free_ip_enties link list.
 */

#ifdef LIMIT_UH
void remove_one_ip(aClient *cptr)
#else
void remove_one_ip(unsigned long ip_in)
#endif
{
  int hash_index;
  IP_ENTRY *last_ptr;
  IP_ENTRY *ptr;
  IP_ENTRY *old_free_ip_entries;
#ifdef LIMIT_UH
  unsigned long ip_in=cptr->ip.s_addr;
  Link *prev_link;
  Link *cur_link;
#endif

  last_ptr = ptr = ip_hash_table[hash_index = hash_ip(ip_in)];
  while(ptr)
    {
      if(ptr->ip == ip_in)
        {
          if(ptr->count != 0)
            ptr->count--;
#ifdef LIMIT_UH

          /* remove the corresponding pointer to this cptr as well */
          prev_link = (Link *)NULL;
          cur_link = ptr->ptr_clients_on_this_ip;

          while(cur_link)
            {
              if(cur_link->value.cptr == cptr)
                {
                  if(prev_link)
                    prev_link->next = cur_link->next;
                  else
                    ptr->ptr_clients_on_this_ip = cur_link->next;
                  free_link(cur_link);
                  break;
                }
              else
                prev_link = cur_link;
              cur_link = cur_link->next;
            }
#endif
          if(ptr->count == 0)
            {
              if(ip_hash_table[hash_index] == ptr)
                ip_hash_table[hash_index] = ptr->next;
              else
                last_ptr->next = ptr->next;
        
              if(free_ip_entries != (IP_ENTRY *)NULL)
                {
                  old_free_ip_entries = free_ip_entries;
                  free_ip_entries = ptr;
                  ptr->next = old_free_ip_entries;
                }
              else
                {
                  free_ip_entries = ptr;
                  ptr->next = (IP_ENTRY *)NULL;
                }
            }
          return;
        }
      else
        {
          last_ptr = ptr;
          ptr = ptr->next;
        }
    }
  sendto_realops("s_conf.c couldn't find ip# in hash table in remove_one_ip()");
  sendto_realops("Please report to the hybrid team! ircd-hybrid@the-project.org");
  return;
}

/*
 * hash_ip()
 * 
 * input        - unsigned long ip address
 * output       - integer value used as index into hash table
 * side effects - hopefully, none
 */

static int hash_ip(unsigned long ip)
{
  int hash;
  ip = ntohl(ip);
  hash = ((ip >> 12) + ip) & (IP_HASH_SIZE-1);
  return(hash);
}

/* Added so s_debug could check memory usage in here -Dianora */
/*
 * count_ip_hash
 *
 * inputs        - pointer to counter of number of ips hashed 
 *               - pointer to memory used for ip hash
 * output        - returned via pointers input
 * side effects  - NONE
 *
 * number of hashed ip #'s is counted up, plus the amount of memory
 * used in the hash.
 */

void count_ip_hash(int *number_ips_stored,u_long *mem_ips_stored)
{
  IP_ENTRY *ip_hash_ptr;
  int i;

  *number_ips_stored = 0;
  *mem_ips_stored = 0;

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];
      while(ip_hash_ptr)
        {
          *number_ips_stored = *number_ips_stored + 1;
          *mem_ips_stored = *mem_ips_stored +
             sizeof(IP_ENTRY);

          ip_hash_ptr = ip_hash_ptr->next;
        }
    }
}


/*
 * iphash_stats()
 *
 * inputs        - 
 * output        -
 * side effects        -
 */
void iphash_stats(aClient *cptr, aClient *sptr,int parc, char *parv[],int out)
{
  IP_ENTRY *ip_hash_ptr;
  int i;
  int collision_count;
  char result_buf[256];

  if(out < 0)
    sendto_one(sptr,":%s NOTICE %s :*** hash stats for iphash",
               me.name,cptr->name);
  else
    {
      (void)sprintf(result_buf,"*** hash stats for iphash\n");
      (void)write(out,result_buf,strlen(result_buf));
    }

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];

      collision_count = 0;
      while(ip_hash_ptr)
        {
          collision_count++;
          ip_hash_ptr = ip_hash_ptr->next;
        }
      if(collision_count)
        {
          if(out < 0)
            {
              sendto_one(sptr,":%s NOTICE %s :Entry %d (0x%X) Collisions %d",
                         me.name,cptr->name,i,i,collision_count);
            }
          else
            {
              (void)sprintf(result_buf,"Entry %d (0x%X) Collisions %d\n",
                            i,i,collision_count);
              (void)write(out,result_buf,strlen(result_buf));
            }
        }
    }
}

/*
 * get_first_nline - return the first N:line in the list
 */
struct ConfItem* find_first_nline(struct SLink* lp)
{
  struct ConfItem* aconf;

  for (; lp; lp = lp->next) {
    aconf = lp->value.aconf;
    if (CONF_NOCONNECT_SERVER == aconf->status)
      return aconf;
  }
  return 0;
}

/*
** detach_conf
**        Disassociate configuration from the client.
**      Also removes a class from the list if marked for deleting.
*/
int detach_conf(struct Client* cptr,struct ConfItem* aconf)
{
  struct SLink** lp;
  struct SLink*  tmp;

  lp = &(cptr->confs);

  while (*lp)
    {
      if ((*lp)->value.aconf == aconf)
        {
          /*
           * NOTE: this is done in free conf too now
           */
          if ((aconf) && (ClassPtr(aconf)))
            {
              if (aconf->status & CONF_CLIENT_MASK)
                {
                  if (ConfLinks(aconf) > 0)
                    --ConfLinks(aconf);
                }
              if (ConfMaxLinks(aconf) == -1 && ConfLinks(aconf) == 0)
                {
                  free_class(ClassPtr(aconf));
                  ClassPtr(aconf) = NULL;
                }
            }
          if (aconf && !--aconf->clients && IsIllegal(aconf))
            {
              free_conf(aconf);
            }
          tmp = *lp;
          *lp = tmp->next;
          free_link(tmp);
          return 0;
        }
      else
        lp = &((*lp)->next);
    }
  return -1;
}

static int is_attached(struct ConfItem *aconf,aClient *cptr)
{
  Link* lp;

  for (lp = cptr->confs; lp; lp = lp->next)
    if (lp->value.aconf == aconf)
      break;
  
  return (lp) ? 1 : 0;
}

/*
 * attach_conf - Associate a specific configuration entry to a *local*
 *        client (this is the one which used in accepting the
 *        connection). Note, that this automatically changes the
 *        attachment if there was an old one...
 */
int attach_conf(aClient *cptr,struct ConfItem *aconf)
{
  Link *lp;

  if (is_attached(aconf, cptr))
    {
      return 1;
    }
  if (IsIllegal(aconf))
    {
      return -1;
    }
  /*
   * By using "ConfLinks(aconf) >= ConfMaxLinks(aconf)....
   * the client limit is set by the Y line, connection class, not
   * by the individual client count in each I line conf.
   *
   * -Dianora
   *
   */

  /* If the requested change, is to turn them into an OPER, then
   * they are already attached to a fd there is no need to check for
   * max in a class now is there?
   *
   * -Dianora
   */

  /* If OLD_Y_LIMIT is defined the code goes back to the old way
   * I lines used to work, i.e. number of clients per I line
   * not total in Y
   * -Dianora
   */
#ifdef OLD_Y_LIMIT
  if ((aconf->status & (CONF_OPER | CONF_ADMINISTRATOR | CONF_CLIENT)) &&
    aconf->clients >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#else
  if ( (aconf->status & (CONF_OPER | CONF_ADMINISTRATOR ) ) == 0 )
    {
      if ((aconf->status & CONF_CLIENT) &&
          ConfLinks(aconf) >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#endif
        {
          if (!IsConfFlined(aconf))
            {
              return -3;        /* Use this for printing error message */
            }
          else
            {
              sendto_one(cptr, ":%s NOTICE %s :*** I: line is full, but you have an >I: line!\n",
                   me.name, cptr->name);
              SetFlined(cptr);
            }

        }
#ifndef OLD_Y_LIMIT
    }
#endif

  lp = make_link();
  lp->next = cptr->confs;
  lp->value.aconf = aconf;
  cptr->confs = lp;
  aconf->clients++;
  if (aconf->status & CONF_CLIENT_MASK)
    ConfLinks(aconf)++;
  return 0;
}


struct ConfItem *find_admin()
{
  struct ConfItem *aconf;

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    if (aconf->status & CONF_ADMINISTRATOR && aconf->user)
      break;
  
  return (aconf);
}

struct ConfItem *find_me()
{
  struct ConfItem *aconf;
  for (aconf = ConfigItemList; aconf; aconf = aconf->next) {
    if (aconf->status & CONF_ME)
      return(aconf);
  }

  Log("Server has no M: line");
  exit (-1);

  assert(0);
  return NULL;        /* oh oh... is there code to handle
                                   this case ? - Dianora */
                                /* There is now... -Dianora */
}

/*
 * attach_confs - Attach all possible CONF lines to a client
 * if the name passed matches that for the conf file (for non-C/N lines) 
 * or is an exact match (C/N lines only).  The difference in behaviour 
 * is to stop C:*::* and N:*::*.
 * returns count of conf entries attached if successful, 0 if none are found
 *
 * NOTE: this will allow C:::* and N:::* because the match mask is the
 * conf line and not the name
 */
int attach_confs(aClient* cptr, const char* name, int statmask)
{
  struct ConfItem* tmp;
  int              conf_counter = 0;
  
  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) && !IsIllegal(tmp) &&
          tmp->name && match(tmp->name, name))
        {
          if (-1 < attach_conf(cptr, tmp))
            ++conf_counter;
        }
      else if ((tmp->status & statmask) && !IsIllegal(tmp) &&
               tmp->name && !irccmp(tmp->name, name))
        {
          if (-1 < attach_conf(cptr, tmp))
            ++conf_counter;
        }
    }
  return conf_counter;
}

/*
 * attach_cn_lines - find C/N lines and attach them to connecting client
 * return true (1) if both are found, otherwise return false (0)
 * called from connect_server
 * NOTE: this requires an exact match between the name on the C:line and
 * the name on the N:line
 */
int attach_cn_lines(aClient *cptr, const char *name, const char* host)
{
  struct ConfItem* tmp;
  int              found_cline = 0;
  int              found_nline = 0; 
  assert(0 != cptr);
  assert(0 != host);

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if (!IsIllegal(tmp))
	{
	  /*
	   * look for matching C:line
	   */
	  if (!found_cline && CONF_CONNECT_SERVER == tmp->status
	      && 
	      tmp->host
	      &&
	      (irccmp(tmp->host, host) == 0)
	      &&
	      tmp->name
	      &&
	      match(tmp->name, name)
	      )
	    {
	      attach_conf(cptr, tmp);
	      if (found_nline)
		return 1;
	      found_cline = 1;
	    }
	  /*
	   * look for matching N:line
	   */
	  else if (!found_nline && CONF_NOCONNECT_SERVER == tmp->status
		   &&
		   tmp->host
		   &&
		   (irccmp(tmp->host, host) == 0)
		   &&
		   match(tmp->name, name))
	    {
	      attach_conf(cptr, tmp);
	      if (found_cline)
		return 1;
	      found_nline = 1;
	    }
	}
    }
  return 0;
}

/*
 * find a conf entry which matches the hostname and has the same name.
 */
struct ConfItem* find_conf_exact(const char* name, const char* user, 
                           const char* host, int statmask)
{
  struct ConfItem *tmp;

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if (!(tmp->status & statmask) || !tmp->name || !tmp->host ||
          irccmp(tmp->name, name))
        continue;
      /*
      ** Accept if the *real* hostname (usually sockethost)
      ** socket host) matches *either* host or name field
      ** of the configuration.
      */
      if (!match(tmp->host, host) || !match(tmp->user,user)
          || irccmp(tmp->name, name) )
        continue;
      if (tmp->status & (CONF_ADMINISTRATOR|CONF_OPER))
        {
          if (tmp->clients < ConfMaxLinks(tmp))
            return tmp;
          else
            continue;
        }
      else
        return tmp;
    }
  return NULL;
}

struct ConfItem* find_conf_name(struct SLink* lp, const char* name, 
                                int statmask)
{
  struct ConfItem* tmp;
  
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if ((tmp->status & statmask) && tmp->name && 
          (!irccmp(tmp->name, name) || match(tmp->name, name)))
        return tmp;
    }
  return NULL;
}

/* 
 * NOTES
 *
 * C:192.168.0.240:password:irc.server.name:...
 * C:irc.server.name:password:irc.server.name
 * C:host:passwd:name:port:class
 * N:host:passwd:name:hostmask number:class
 */
/*
 * Added for new access check    meLazy <- no youShithead, your code sucks
 */
struct ConfItem* find_conf_host(struct SLink* lp, const char* host, 
                                int statmask)
{
  struct ConfItem *tmp;
  
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (tmp->status & statmask && tmp->host && match(tmp->host, host))
        return tmp;
    }
  return NULL;
}

/*
 * find_conf_ip
 *
 * Find a conf line using the IP# stored in it to search upon.
 * Added 1/8/92 by Avalon.
 */
struct ConfItem *find_conf_ip(struct SLink* lp, char *ip, char *user, 
                              int statmask)
{
  struct ConfItem *tmp;
  
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (!(tmp->status & statmask))
        continue;

      if (!match(tmp->user, user))
        {
          continue;
        }

      if (!memcmp((void *)&tmp->ipnum, (void *)ip, sizeof(struct in_addr)))
        return tmp;
    }
  return ((struct ConfItem *)NULL);
}

/*
 * find_conf_by_name - return a conf item that matches name and type
 */
struct ConfItem* find_conf_by_name(const char* name, int status)
{
  struct ConfItem* conf;
  assert(0 != name);
 
  for (conf = ConfigItemList; conf; conf = conf->next)
    {
      if (conf->status == status && conf->name &&
          match(name, conf->name))
#if 0
          (match(name, conf->name) || match(conf->name, name)))
#endif
        return conf;
    }
  return NULL;
}

/*
 * find_conf_by_host - return a conf item that matches host and type
 */
struct ConfItem* find_conf_by_host(const char* host, int status)
{
  struct ConfItem* conf;
  assert(0 != host);
 
  for (conf = ConfigItemList; conf; conf = conf->next)
    {
      if (conf->status == status && conf->host &&
          match(host, conf->host))
#if 0
          (match(host, conf->host) || match(conf->host, host)))
#endif
        return conf;
    }
  return NULL;
}

/*
 * find_conf_entry
 *
 * - looks for a match on all given fields.
 */
struct ConfItem *find_conf_entry(struct ConfItem *aconf, int mask)
{
  struct ConfItem *bconf;

  for (bconf = ConfigItemList, mask &= ~CONF_ILLEGAL; bconf; 
       bconf = bconf->next)
    {
      if (!(bconf->status & mask) || (bconf->port != aconf->port))
        continue;
      
      if ((BadPtr(bconf->host) && !BadPtr(aconf->host)) ||
          (BadPtr(aconf->host) && !BadPtr(bconf->host)))
        continue;

      if (!BadPtr(bconf->host) && irccmp(bconf->host, aconf->host))
        continue;

      if ((BadPtr(bconf->passwd) && !BadPtr(aconf->passwd)) ||
          (BadPtr(aconf->passwd) && !BadPtr(bconf->passwd)))
        continue;

      if (!BadPtr(bconf->passwd) &&
          irccmp(bconf->passwd, aconf->passwd))
      continue;

      if ((BadPtr(bconf->name) && !BadPtr(aconf->name)) ||
          (BadPtr(aconf->name) && !BadPtr(bconf->name)))
        continue;

      if (!BadPtr(bconf->name) && irccmp(bconf->name, aconf->name))
        continue;
      break;
    }
  return bconf;
}

/*
 * find_special_conf
 *
 * inputs       - pointer to char string to find
 *              - mask of type of conf to compare on
 * output       - NULL or pointer to found struct ConfItem
 * side effects - looks for a match on name field
 */
struct ConfItem *find_special_conf(char *to_find, int mask)
{
  struct ConfItem *aconf;
  struct ConfItem *this_conf;

  if(mask & CONF_XLINE)
    this_conf = x_conf;
  else if(mask & CONF_ULINE)
    this_conf = u_conf;
  else
    return((struct ConfItem *)NULL);

  for (aconf = this_conf; aconf; aconf = aconf->next)
    {
      if (!(aconf->status & mask))
        continue;
      
      if (BadPtr(aconf->name))
          continue;

      if(match(aconf->name,to_find))
        return aconf;

    }
  return((struct ConfItem *)NULL);
}

/*
 * find_q_line
 *
 * inputs       - nick to find
 *              - user to match
 *              - host to mask
 * output       - YES if found, NO if not found
 * side effects - looks for matches on Q lined nick
 */
int find_q_line(char *nickToFind,char *user,char *host)
{
  aQlineItem *qp;
  struct ConfItem *aconf;

  for (qp = q_conf; qp; qp = qp->next)
    {
      if (BadPtr(qp->name))
          continue;

      if(match(qp->name,nickToFind))
        {
          if(qp->confList)
            {
              for(aconf=qp->confList;aconf;aconf=aconf->next)
                {
                  if(match(aconf->user,user) && match(aconf->host,host))
                    return NO;
                }
            }
          return YES;
        }
    }
  return NO;
}

/*
 * clear_q_lines
 *
 * inputs       - none
 * output       - none
 * side effects - clear out the q lines
 */
static void clear_q_lines()
{
  aQlineItem *qp;
  aQlineItem *qp_next;
  struct ConfItem *aconf;
  struct ConfItem *next_aconf;

  for (qp = q_conf; qp; qp = qp_next)
    {
      qp_next = qp->next;

      if(qp->name)
        {
          MyFree(qp->name);
          qp->name = (char *)NULL;
        }

      if (qp->confList)
        {
          for (aconf = qp->confList; aconf; aconf = next_aconf)
            {
              next_aconf = aconf->next;
              free_conf(aconf);
            }
        }
      MyFree(qp);
    }
  q_conf = (aQlineItem *)NULL;
}

/*
 * report_qlines
 *
 * inputs       - pointer to client to report to
 * output       - none
 * side effects - all Q lines are listed to client 
 */

void report_qlines(aClient *sptr)
{
  aQlineItem *qp;
  struct ConfItem *aconf;
  char *host;
  char *user;
  char *pass;
  char *name;
  int port;

  for (qp = q_conf; qp; qp = qp->next)
    {
      if(!qp->name)
        {
          continue;
        }

      for (aconf=qp->confList;aconf;aconf = aconf->next)
        {
          get_printable_conf(aconf, &name, &host, &pass, &user, &port);
          
          sendto_one(sptr, form_str(RPL_STATSQLINE),
                     me.name, sptr->name, 'Q', name, user, host, pass);
        }
    }
}

#ifdef JUPE_CHANNEL
/* clear_juped_channels()
 *
 * inputs       -
 * output       -
 * side effects - clears all channel jupes that were added in the conf
 */
void clear_juped_channels()
{
  aQlineItem *qp;
  struct ConfItem *aconf;
  struct Channel *chptr;
  char *host, *user, *pass, *name;
  int port;

  for(qp = q_conf; qp; qp = qp->next)
  {
    if(!qp->name)
      continue;

    for(aconf = qp->confList; aconf; aconf = aconf->next)
    {
      get_printable_conf(aconf, &name, &host, &pass, &user, &port);

      if((*name == '#') && (chptr = hash_find_channel(name, NULL)))
        chptr->juped = 0;
    }
  }
}
#endif /* JUPE_CHANNEL */

/*
 * add_q_line
 *
 * inputs       - pointer to aconf to add
 * output       - none
 * side effects - given Q line is added to q line list 
 */

static void add_q_line(struct ConfItem *aconf)
{
  char *pc;
  aQlineItem *qp, *newqp;
  char *uath;

  if(!aconf->user)
    DupString(aconf->user, "-");

  for (qp = q_conf; qp; qp = qp->next)
    {
      if(!qp->name)
        {
          continue;
        }

      if(!irccmp(aconf->name,qp->name))
        {
          if (qp->confList)
            {
              /* 
               * - Slowaris
               */
              
              uath = strtoken(&pc, aconf->user, ",");
              if(!uath)
                {
                  uath = aconf->user;
                  makeQlineEntry(qp, aconf, uath);
                }
              else
                {
                  for( ; uath; uath = strtoken(&pc,NULL,","))
                    {
                      makeQlineEntry(qp, aconf, uath);
                    }
                }
            }
          free_conf(aconf);
          return;
        }
    }

  newqp = (aQlineItem *)MyMalloc(sizeof(aQlineItem));
  newqp->confList = (struct ConfItem *)NULL;
  DupString(newqp->name,aconf->name);
  newqp->next = q_conf;
  q_conf = newqp;

  /* 
   * - Slowaris
   */

  uath = strtoken(&pc, aconf->user, ",");
  if(!uath)
    {
      uath = aconf->user;
      makeQlineEntry(newqp, aconf, uath);
    }
  else
    {
      for ( ;uath; uath = strtoken(&pc,(char *)NULL,","))
        {
          makeQlineEntry(newqp, aconf, uath);
        }
    }

  free_conf(aconf);
}

static void makeQlineEntry(aQlineItem *qp, struct ConfItem *aconf, char *uath)
{
  char *p,*comu,*comh;
  struct ConfItem *bconf;

  p = strchr(uath, '@');
  if(!p)
    {
      DupString(comu,"-");
      DupString(comh,"-");
    }
  else
    {
      *p = '\0';
      DupString(comu,uath);
      p++;
      DupString(comh,p);
    }
                  
  bconf = make_conf();
  DupString(bconf->name, aconf->name);
  if(aconf->passwd)
    DupString(bconf->passwd,aconf->passwd);
  else
    DupString(bconf->passwd, "No Reason");
  bconf->user = comu;
  bconf->host = comh;
  bconf->next = qp->confList;
  qp->confList = bconf;
}

/*
 * clear_special_conf
 * 
 * inputs       - pointer to pointer of root of special conf link list
 * output       - none
 * side effects - clears given special conf lines
 */
static void clear_special_conf(struct ConfItem **this_conf)
{
  struct ConfItem *aconf;
  struct ConfItem *next_aconf;

  for (aconf = *this_conf; aconf; aconf = next_aconf)
    {
      next_aconf = aconf->next;
      free_conf(aconf);
    }
  *this_conf = (struct ConfItem *)NULL;
  return;
}

/*
 * rehash_dump
 *
 * inputs       - pointer to client requesting rehash dump
 * output       -
 * side effects -
 * partially reconstruct an ircd.conf file (tsk tsk, you should have
 * been making backups;but we've all done it)
 * I just cull out the N/C/O/o/A lines, you'll have to finish
 * the rest to use this dump.
 *
 * -Dianora
 */

int rehash_dump(aClient *sptr)
{
  struct ConfItem *aconf;
  FBFILE* out;
  char ircd_dump_file[256];
  char result_buf[256];
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  char *host;
  char *pass;
  char *user;
  char *name;
  int  port;

  tmptr = localtime(&CurrentTime);
  strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d%H%M", tmptr);
  if (DPATH[strlen(DPATH)-1] == '/')
     (void)sprintf(ircd_dump_file,"%sircd.conf.%s",
                   DPATH,timebuffer);
  else
     (void)sprintf(ircd_dump_file,"%s/ircd.conf.%s", 
		   DPATH, timebuffer);
  
  if ((out = fbopen(ircd_dump_file, "a")) == 0)
    {
      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
                 me.name, sptr->name, ircd_dump_file);
      return -1;
    }
  else
    sendto_one(sptr, ":%s NOTICE %s :Dump ircd.conf to %s ",
               me.name, sptr->name, ircd_dump_file);

  for(aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      aClass* class_ptr = ClassPtr(aconf);
      get_printable_conf(aconf, &name, &host, &pass, &user, & port );

      if(aconf->status == CONF_CONNECT_SERVER)
        {
          
          (void)sprintf(result_buf,"C:%s:%s:%s::%d\n",
                        host,pass,
                        name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_NOCONNECT_SERVER)
        {
          (void)sprintf(result_buf,"N:%s:%s:%s::%d\n",
                        host,pass,
                        name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_ADMINISTRATOR)
        {
          (void)sprintf(result_buf,"O:%s@%s:%s:%s::%d\n",
                        user,host,
                        pass,
                        name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_OPER)
        {
          (void)sprintf(result_buf,"o:%s@%s:%s:%s::%d\n",
                        user,host,
                        pass,
                        name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_ADMINISTRATOR)
        {
          (void)sprintf(result_buf,"A:%s:%s:%s::\n",
                        host,pass,user);
          fbputs(result_buf, out);
        }
    }
  fbclose(out);
  return 0;
}

/*
 * rehash
 *
 * Actual REHASH service routine. Called with sig == 0 if it has been called
 * as a result of an operator issuing this command, else assume it has been
 * called as a result of the server receiving a HUP signal.
 */
int rehash(aClient *cptr,aClient *sptr, int sig)
{
  if (sig)
    sendto_realops("Got signal SIGHUP, reloading ircd conf. file");

  read_conf_files(NO);
  close_listeners();
  flush_deleted_I_P();
  rehashed = 1;
  return 0;
}

/*
 * openconf
 *
 * returns 0 (NULL) on any error or else the fd opened from which to read the
 * configuration file from.  
 */
static FBFILE* openconf(const char *filename)
{
  return fbopen(filename, "r");
}

/*
** from comstud
*/

static char *set_conf_flags(struct ConfItem *aconf,char *tmp)
{
  for(;*tmp;tmp++)
    {
      switch(*tmp)
        {
        case '=':
          aconf->flags |= CONF_FLAGS_SPOOF_IP;
          break;
        case '!':
          aconf->flags |= CONF_FLAGS_LIMIT_IP;
          break;
        case '-':
          aconf->flags |= CONF_FLAGS_NO_TILDE;
          break;
        case '+':
          aconf->flags |= CONF_FLAGS_NEED_IDENTD;
          break;
        case '$':
          aconf->flags |= CONF_FLAGS_PASS_IDENTD;
          break;
        case '%':
          aconf->flags |= CONF_FLAGS_NOMATCH_IP;
          break;
        case '^':        /* is exempt from k/g lines */
          aconf->flags |= CONF_FLAGS_E_LINED;
          break;
        case '&':        /* obsolete flag was "can run a bot" */
          break;
        case '>':        /* can exceed max connects */
          aconf->flags |= CONF_FLAGS_F_LINED;
          break;
        case '_':        /* exempt from glines */
          aconf->flags |= CONF_FLAGS_EXEMPTGLINE;
          break;
#ifdef IDLE_CHECK
        case '<':        /* can idle */
          aconf->flags |= CONF_FLAGS_IDLE_LINED;
          break;
#endif
        default:
          return tmp;
        }
    }
  return tmp;
}

/*
** initconf() 
**    Read configuration file.
**
*
* Inputs        - file descriptor pointing to config file to use
*               - int (included file = 1, original (ircd.conf) = 0)
*
**    returns -1, if file cannot be opened
**             0, if file opened
*/

#define MAXCONFLINKS 150

static void initconf(FBFILE* file, int use_include)
{
  static char  quotes[] = {
    0,    /*  */
    0,    /* a */
    '\b', /* b */
    0,    /* c */
    0,    /* d */
    0,    /* e */
    '\f', /* f */
    0,    /* g */
    0,    /* h */
    0,    /* i */
    0,    /* j */
    0,    /* k */
    0,    /* l */
    0,    /* m */
    '\n', /* n */
    0,    /* o */
    0,    /* p */
    0,    /* q */
    '\r', /* r */
    0,    /* s */
    '\t', /* t */
    0,    /* u */
    '\v', /* v */
    0,    /* w */
    0,    /* x */
    0,    /* y */
    0,    /* z */
    0,0,0,0,0,0 
    };

  char*            tmp;
  char*            s;
  int              dontadd;
  char             line[BUFSIZE];
  int              ccount = 0;
  int              ncount = 0;
  struct ConfItem* aconf = NULL;
  struct ConfItem* include_conf = NULL;
  unsigned long    ip;
  unsigned long    ip_mask;
  int              sendq = 0;
  aClass*          class0;

  class0 = find_class(0);        /* which one is class 0 ? */

  while (fbgets(line, sizeof(line), file))
    {
      if ((tmp = strchr(line, '\n')))
        *tmp = '\0';

      /*
       * Do quoting of characters and # detection.
       */
      for (tmp = line; *tmp; tmp++)
        {
          if (*tmp == '\\')
	    {
	      if ( *(tmp+1) == '\\' )
                *tmp = '\\';
	      else
                *tmp = quotes[ (unsigned int) (*(tmp+1) & 0x1F) ];
	      for (s = tmp; (*s = *(s+1)); s++)
		;
            }
          else if (*tmp == '#')
            *tmp = '\0';
        }
      if (!*line || line[0] == '#' || line[0] == '\n' ||
          line[0] == ' ' || line[0] == '\t')
        continue;

      /* Horrible kludge to do .include "filename" */

      if(use_include && (line[0] == '.'))
        {
          char *filename;
          char *back;

          if(!ircncmp(line+1,"include ",8))
            {
              if( (filename = strchr(line+8,'"')) )
                filename++;
              else
                {
                  Log("Bad config line: %s", line);
                  continue;
                }

              if( (back = strchr(filename,'"')) )
                *back = '\0';
              else
                {
                  Log("Bad config line: %s", line);
                  continue;
                }
              include_conf = make_conf();
              DupString(include_conf->name,filename);
              include_conf->next = include_list;
              include_list = include_conf;
            }
          /* 
           * A line consisting of the first char '.' will now
           * be treated as a comment line.
           * a line `.include "file"' will result in an included
           * portion of the conf file.
           */
          continue;
        }

      /* Could we test if it's conf line at all?        -Vesa */
      if (line[1] != ':')
        {
          Log("Bad config line: %s", line);
          continue;
        }
      if (aconf)
        free_conf(aconf);
      aconf = make_conf();

      tmp = getfield(line);
      if (!tmp)
        continue;
      dontadd = 0;
      switch (*tmp)
        {
        case 'A': /* Name, e-mail address of administrator */
        case 'a': /* of this server. */
          aconf->status = CONF_ADMINISTRATOR;
          break;

        case 'C': /* Server where I should try to connect */
                    /* in case of lp failures             */
          ccount++;
          aconf->status = CONF_CONNECT_SERVER;
          aconf->flags = CONF_FLAGS_ALLOW_AUTO_CONN;
          break;

        case 'c':
          ccount++;
          aconf->status = CONF_CONNECT_SERVER;
          aconf->flags = CONF_FLAGS_ALLOW_AUTO_CONN|CONF_FLAGS_ZIP_LINK;
          break;

        case 'd':
          aconf->status = CONF_DLINE;
          aconf->flags = CONF_FLAGS_E_LINED;
          break;
        case 'D': /* Deny lines (immediate refusal) */
          aconf->status = CONF_DLINE;
          break;

        case 'H': /* Hub server line */
        case 'h':
          aconf->status = CONF_HUB;
          break;

#ifdef LITTLE_I_LINES
        case 'i': /* Just plain normal irc client trying  */
                  /* to connect to me */
          aconf->status = CONF_CLIENT;
          aconf->flags |= CONF_FLAGS_LITTLE_I_LINE;
          break;

        case 'I': /* Just plain normal irc client trying  */
                  /* to connect to me */
          aconf->status = CONF_CLIENT;
          break;
#else
        case 'i': /* Just plain normal irc client trying  */
        case 'I': /* to connect to me */
          aconf->status = CONF_CLIENT;
          break;
#endif
        case 'K': /* Kill user line on irc.conf           */
        case 'k':
          aconf->status = CONF_KILL;
          break;

        case 'L': /* guaranteed leaf server */
        case 'l':
          aconf->status = CONF_LEAF;
          break;

          /* Me. Host field is name used for this host */
          /* and port number is the number of the port */
        case 'M':
        case 'm':
          aconf->status = CONF_ME;
          break;

        case 'N': /* Server where I should NOT try to     */
        case 'n': /* connect in case of lp failures     */
          /* but which tries to connect ME        */
          ++ncount;
          aconf->status = CONF_NOCONNECT_SERVER;
          break;

          /* Operator. Line should contain at least */
          /* password and host where connection is  */

        case 'O':
          aconf->status = CONF_ADMINISTRATOR;
          break;
          /* Local Operator, (limited privs --SRB) */

        case 'o':
          aconf->status = CONF_OPER;
          break;

        case 'P': /* listen port line */
        case 'p':
          aconf->status = CONF_LISTEN_PORT;
          break;

        case 'Q': /* reserved nicks */
        case 'q': 
          aconf->status = CONF_QUARANTINED_NICK;
          break;

        case 'U': /* Uphost, ie. host where client reading */
        case 'u': /* this should connect.                  */
          aconf->status = CONF_ULINE;
          break;

        case 'X': /* rejected gecos */
        case 'x': 
          aconf->status = CONF_XLINE;
          break;

        case 'Y':
        case 'y':
          aconf->status = CONF_CLASS;
          sendq = 0;
          break;

        default:
          Log("Error in config file: %s", line);
          break;
        }
      if (IsIllegal(aconf))
        continue;

      for (;;) /* Fake loop, that I can use break here --msa */
        {
	  char *p;

	  /* host field */
          if ((tmp = getfield(NULL)) == NULL)
            break;
          /*from comstud*/
          if(aconf->status & CONF_CLIENT)
            tmp = set_conf_flags(aconf, tmp);
          DupString(aconf->host, tmp);

	  /* pass field */
          if ((tmp = getfield(NULL)) == NULL)
            break;

	  if ((p = strchr(tmp, '|')) != NULL)
	  {
	    *p = '\0';
	    DupString(aconf->passwd, tmp);
	  }
	  else
	  {
	    DupString(aconf->passwd, tmp);
	  }

	  /* user field */
          if ((tmp = getfield(NULL)) == NULL)
            break;
          /*from comstud */

          if(aconf->status & CONF_CLIENT)
            tmp = set_conf_flags(aconf, tmp);
          DupString(aconf->user, tmp);

	  /* port field */
          if(aconf->status & CONF_ADMINISTRATOR)
            {
              /* defaults */
              aconf->port = 
                CONF_OPER_GLOBAL_KILL|CONF_OPER_REMOTE|CONF_OPER_UNKLINE|
                CONF_OPER_K|CONF_OPER_GLINE|CONF_OPER_REHASH;
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = oper_privs_from_string(aconf->port,tmp);
            }
          else if(aconf->status & CONF_OPER)
            {
              aconf->port = CONF_OPER_UNKLINE|CONF_OPER_K;
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = oper_privs_from_string(aconf->port,tmp);
            }
          else
            {
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = atoi(tmp);
            }

          Debug((DEBUG_DEBUG,"aconf->port %x",aconf->port));

	  /* class field */
          if ((tmp = getfield(NULL)) == NULL)
            break;
          Debug((DEBUG_DEBUG,"class tmp = %s",tmp));

          if(aconf->status & CONF_CLASS)
            {
              sendq = atoi(tmp);
            }
          else
            {
              int classToFind;

              classToFind = atoi(tmp);

              ClassPtr(aconf) = find_class(classToFind);

              if(classToFind && (ClassPtr(aconf) == class0))
                {
                  sendto_realops(
                           "Warning *** Defaulting to class 0 for class %d",
                         classToFind);
                }
            }

          if(aconf->status & (CONF_OPER|CONF_ADMINISTRATOR))
            {
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->hold = oper_flags_from_string(tmp);
            }
          else if(aconf->status & CONF_CONNECT_SERVER)
            {
              if ((tmp = getfield(NULL)) == NULL)
                break;
              (void)is_address(tmp, &aconf->ip, &aconf->ip_mask);
            }

          break;
          /* NOTREACHED */
        }

      /* For Gersh
       * make sure H: lines don't have trailing spaces!
       * BUG: This code will fail if there is leading whitespace.
       */

      if( aconf->status & (CONF_HUB|CONF_LEAF) )
        {
          char *ps;        /* space finder */
          char *pt;        /* tab finder */

	  if(!aconf->user)
	    {
	      DupString(aconf->name, "*");
	      DupString(aconf->user, "*");
	    }
	  else
	    {
	      ps = strchr(aconf->user,' ');
	      pt = strchr(aconf->user,'\t');

	      if(ps || pt)
		{
		  sendto_realops("H: or L: line trailing whitespace [%s]",
				 aconf->user);
		  if(ps)*ps = '\0';
		  if(pt)*pt = '\0';
		}
	      aconf->name = aconf->user;
	      DupString(aconf->user, "*");
	    }
        }

      /*
      ** If conf line is a class definition, create a class entry
      ** for it and make the conf_line illegal and delete it.
      ** Negative class numbers are not accepted.
      */
      if (aconf->status & CONF_CLASS && atoi(aconf->host) > -1)
        {
          add_class(atoi(aconf->host), atoi(aconf->passwd),
                    atoi(aconf->user), aconf->port,
                    sendq );
          continue;
        }
      /*
      ** associate each conf line with a class by using a pointer
      ** to the correct class record. -avalon
      */

      /*
       * P: line - listener port
       */
      if ( aconf->status & CONF_LISTEN_PORT)
        {
          dontadd = 1;
          if((aconf->passwd[0] == '\0') || (aconf->passwd[0] == '*'))
            add_listener(aconf->port, NULL );
          else
            add_listener(aconf->port, (const char *)aconf->passwd);
        }
      else if(aconf->status & CONF_CLIENT_MASK)
        {
          if (0 == ClassPtr(aconf))
            ClassPtr(aconf) = find_class(0);
          if (ConfMaxLinks(aconf) < 0)
            ClassPtr(aconf) = find_class(0);
        }

      /* I: line */
      if (aconf->status & CONF_CLIENT)
        {
          struct ConfItem *bconf;
          
          if ((bconf = find_conf_entry(aconf, aconf->status)))
            {
              delist_conf(bconf);
              bconf->status &= ~CONF_ILLEGAL;
              if (aconf->status == CONF_CLIENT)
                {
                  ConfLinks(bconf) -= bconf->clients;
                  ClassPtr(bconf) = ClassPtr(aconf);
                  ConfLinks(bconf) += bconf->clients;
                  /*
                   * still... I've munged the flags possibly
                   * so update the found struct ConfItem for now 
                   * -Dianora
                   */
                  bconf->flags = aconf->flags;
                  if(bconf->flags & (CONF_OPER|CONF_ADMINISTRATOR))
                    bconf->port = aconf->port;
                }
              free_conf(aconf);
              aconf = bconf;
            }
        }

      /* If the conf entry specificed is a C/N line... */
      if (aconf->status & CONF_SERVER_MASK)
        {
          if (ncount > MAXCONFLINKS || ccount > MAXCONFLINKS ||
              !aconf->host || !aconf->user)
            {
              Log("Bad C/N line");
              sendto_realops("Bad C/N line");
              continue;
            }

          if (BadPtr(aconf->passwd))
            {
              Log("Bad C/N line passwd for %s");
              sendto_realops("Bad C/N line passwd for %s", aconf->host);
              continue;
            }

          if (SplitUserHost(aconf) < 0)
            {
              Log("Bad C/N line user/host portion for %s");
              sendto_realops("Bad C/N line user/host portion for %s",
                             aconf->host);
              free_conf(aconf);
              aconf = NULL;
              continue;
            }

          lookup_confhost(aconf);
        }

      /* o: or O: line */

      if (aconf->status & (CONF_OPER|CONF_ADMINISTRATOR))
        {
          if(SplitUserHost(aconf) < 0)
            {
              sendto_realops("Bad O/o line host %s", aconf->host);
              free_conf(aconf);
              aconf = NULL;
	      continue;
            }
        }

      /*
      ** Own port and name cannot be changed after the startup.
      ** (or could be allowed, but only if all links are closed
      ** first).
      ** Configuration info does not override the name and port
      ** if previously defined. Note, that "info"-field can be
      ** changed by "/rehash".
      ** Can't change vhost mode/address either 
      */
      if (aconf->status == CONF_ME)
        {
          strncpy_irc(me.info, aconf->user, REALLEN);

          if (me.name[0] == '\0' && aconf->host[0])
          {
            strncpy_irc(me.name, aconf->host, HOSTLEN);
            if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
            {
                memset(&vserv,0, sizeof(vserv));
                vserv.sin_family = AF_INET;
                vserv.sin_addr.s_addr = inet_addr(aconf->passwd);
                specific_virtual_host = 1;
            }
          }
        }
      else if (aconf->host && (aconf->status & CONF_CLIENT))
        {
          char *p;
          unsigned long ip_host;
          unsigned long ip_lmask;
          dontadd = 1;
          
          if(aconf->host == NULL)
            DupString(aconf->host,"*");
          else
            (void)collapse(aconf->host);

          if(aconf->user == NULL)
            DupString(aconf->user,"*");
          else
            (void)collapse(aconf->user);

          /* The idea here is, to separate a name@host part
           * into aconf->host part and aconf->user part
           * If the user@host part is found in the aconf->host field
           * from conf file, then it has to be an IP I line.
           */

          MyFree(aconf->name); /* should be already NULL here */

          /* Keep a copy of the original host part in "name" */
          DupString(aconf->name,aconf->host);

          /* see if the user@host part is on the 'left side'
           * in the aconf->host field. If it is, then it should be
           * an IP I line only, but I won't enforce it here. 
           */

          if( (p = strchr(aconf->host,'@')))
            {
              char* x;
              aconf->flags |= CONF_FLAGS_DO_IDENTD;
              *p++ = '\0';
              MyFree(aconf->user);
              DupString(aconf->user, aconf->host);
              DupString(x, p);
              MyFree(aconf->host);
              aconf->host = x;
            }

           if( is_address(aconf->host,&ip_host,&ip_lmask) )
	     {
               aconf->ip = ip_host & ip_lmask;
               aconf->ip_mask = ip_lmask;
               add_ip_Iline(aconf);
             }
           else
	     {
	       /* See if there is a name@host part on the 'right side'
		* in the aconf->name field.
		*/

	       if( (p = strchr(aconf->user,'@')) )
		 {
		   aconf->flags |= CONF_FLAGS_DO_IDENTD;
		   *p = '\0';
		   p++;
		   MyFree(aconf->host);
		   DupString(aconf->host,p);
		 }
	       else
		 {
		   MyFree(aconf->host);
		   aconf->host = aconf->user;
		   DupString(aconf->user,"*");
		 }
	       add_mtrie_conf_entry(aconf,CONF_CLIENT);
	     }
        }
      else if (aconf->host && (aconf->status & CONF_KILL))
        {
          dontadd = 1;
          if(is_address(aconf->host,&ip,&ip_mask))
            {
              ip &= ip_mask;
              aconf->ip = ip;
              aconf->ip_mask = ip_mask;
              add_ip_Kline(aconf);
            }
          else
            {
              (void)collapse(aconf->host);
              (void)collapse(aconf->user);
              add_mtrie_conf_entry(aconf,CONF_KILL);
            }
        }
      else if (aconf->host && (aconf->status & CONF_DLINE))
        {
          dontadd = 1;
          DupString(aconf->user,aconf->host);
          (void)is_address(aconf->host,&ip,&ip_mask);
          ip &= ip_mask;
          aconf->ip = ip;
          aconf->ip_mask = ip_mask;

          if(aconf->flags & CONF_FLAGS_E_LINED)
            add_dline(aconf);
          else
            add_Dline(aconf);
        }
      else if (aconf->status & CONF_XLINE)
        {
          dontadd = 1;
          MyFree(aconf->user);
          aconf->user = NULL;
          aconf->name = aconf->host;
          aconf->host = (char *)NULL;
          aconf->next = x_conf;
          x_conf = aconf;
        }
      else if (aconf->status & CONF_ULINE)
        {
          dontadd = 1;
          MyFree(aconf->user);
          aconf->user = (char *)NULL;
          aconf->name = aconf->host;
          aconf->host = (char *)NULL;
          aconf->next = u_conf;
          u_conf = aconf;
        }
      else if (aconf->status & CONF_QUARANTINED_NICK)
        {
          dontadd = 1;
          aconf->name = aconf->host;
          DupString(aconf->host, "*");

#ifdef JUPE_CHANNEL
          if(aconf->name[0] == '#')
            {
              aChannel *chptr;
              int len;

              if( (chptr = hash_find_channel(aconf->name, (aChannel *)NULL)) )
                chptr->juped = 1;
              else
                {
                  /* create a zero user channel, marked as juped
                   * which just place holds the channel down.
                   */

                  len = strlen(aconf->name);
                  chptr = (aChannel*) MyMalloc(sizeof(aChannel) + len + 1);
                  memset(chptr, 0, sizeof(aChannel));
                  /*
                   * NOTE: strcpy ok since we already know the length
                   */
                  strcpy(chptr->chname, aconf->name);
                  chptr->juped = 1;
                  if (channel)
                    channel->prevch = chptr;
                  chptr->prevch = NULL;
                  chptr->nextch = channel;
                  channel = chptr;
                  /* JIC */
                  chptr->channelts = CurrentTime;
                  (void)add_to_channel_hash_table(aconf->name, chptr);
                  Count.chan++;
                }

              if(aconf->passwd)
                strncpy_irc(chptr->topic, aconf->passwd, TOPICLEN);
            }
#endif

          /* host, password, name, port, class */
          /* nick, reason, user@host */
          
          add_q_line(aconf);
        }

      if (!dontadd)
        {
          (void)collapse(aconf->host);
          (void)collapse(aconf->user);
          Debug((DEBUG_NOTICE,
                 "Read Init: (%d) (%s) (%s) (%s) (%d)",
                 aconf->status,
		 (aconf->host)?(aconf->host):"NULL",
		 (aconf->passwd)?(aconf->passwd):"NULL",
                 (aconf->user)?(aconf->user):"NULL",
		 aconf->port));
          aconf->next = ConfigItemList;
          ConfigItemList = aconf;
        }
      aconf = NULL;
    }
  if (aconf)
    free_conf(aconf);
  aconf = NULL;

  fbclose(file);
  check_class();
  nextping = nextconnect = time(NULL);

  if(me.name[0] == '\0')
    {
      Log("Server has no M: line");
      exit(-1);
    }
}

/*
 * commonly used function to split user@host part into user and host fields
 */

static int SplitUserHost(struct ConfItem *aconf)
{
  char *p;

  if ( (p = strchr(aconf->host, '@')) )
    {
      *p = '\0';
      p++;
      if(aconf->user)
        {
          aconf->name = aconf->user;
          DupString(aconf->user, aconf->host);
          strcpy(aconf->host, p);
        }
      else
        {
          return(-1);
        }
    }
  else
    {
      if(aconf->user)
        {
          aconf->name = aconf->user;
          DupString(aconf->user, "*");
        }
    }
  return(1);
}

/*
 * do_include_conf()
 *
 * inputs        - NONE
 * output        - NONE
 * side effect        -
 * hash in any .include conf files listed in the conf file
 * -Dianora
 */

static void do_include_conf(void)
{
  FBFILE* file=0;
  struct ConfItem *nextinclude;

  for( ; include_list; include_list = nextinclude )
    {
      nextinclude = include_list->next;
      if ((file = openconf(include_list->name)) == 0)
        sendto_realops("Can't open %s include file",include_list->name);
      else
        {
          sendto_realops_flags(FLAGS_DEBUG, "Hashing in %s include file",include_list->name);
          initconf(file, NO);
        }
      free_conf(include_list);
    }
}

/*
 * lookup_confhost - start DNS lookups of all hostnames in the conf
 * line and convert an IP addresses in a.b.c.d number for to IP#s.
 *
 */
static void lookup_confhost(struct ConfItem* aconf)
{
  unsigned long    ip;
  unsigned long    mask;

  if (BadPtr(aconf->host) || BadPtr(aconf->name))
    {
      Log("Host/server name error: (%s) (%s)",
          aconf->host, aconf->name);
      return;
    }

  /*
  ** Do name lookup now on hostnames given and store the
  ** ip numbers in conf structure.
  */
  if (is_address(aconf->host, &ip, &mask))
    {
      aconf->ipnum.s_addr = htonl(ip);
    }
  else 
     conf_dns_lookup(aconf);
}

/*
 * conf_connect_allowed (untested)
 */
int conf_connect_allowed(struct in_addr addr)
{
  struct ConfItem *aconf = match_Dline(ntohl((unsigned long)addr.s_addr));

  if (aconf && !IsConfElined(aconf))
    return 0;
  return 1;
}

/*
 * find_kill
 *
 * See if this user is klined already, and if so, return struct ConfItem pointer
 * to the entry for this kline. This wildly changes the way find_kill works
 * -Dianora
 *
 */
struct ConfItem *find_kill(aClient* cptr)
{
  assert(cptr != NULL);
  /* If client is e-lined, then its not k-linable */
  /* opers get that flag automatically, normal users do not */
  return (IsElined(cptr)) ? 0 : find_is_klined(cptr->host, 
                                               cptr->username, 
                                               cptr->ip.s_addr);
}

/*
 * expire_temp_klines
 *
 * inputs        - pointer to root of tkline list
 * output        - none
 * side effects  -
 *
 */
static void
expire_temp_klines(struct ConfItem **tklines)
{
  struct ConfItem *cur_p; 
  struct ConfItem *next_p;
  struct ConfItem *last_p;

  for (last_p = NULL, cur_p = *tklines; cur_p; cur_p = next_p)
  {   
    next_p = cur_p->next;

    if(cur_p->hold <= CurrentTime)        /* a kline has expired */
    {
      if(last_p != NULL)
        last_p->next = cur_p->next;
      else
        *tklines = cur_p->next;

      /* Alert opers that a TKline expired - Hwy */
      sendto_realops("Temporary K-line for [%s@%s] expired",
                     (cur_p->user) ? cur_p->user : "*",
                     (cur_p->host) ? cur_p->host : "*");
      free_conf(cur_p);
    }
    else
      last_p = cur_p;
  }
}

/*
 * find_tkline
 *
 * inputs        - hostname
 *               - username
 * output        - matching struct ConfItem or NULL
 * side effects  -
 *
 * WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

static struct ConfItem* 
find_tkline(const char* host, const char* user, unsigned long ip)
{
  struct ConfItem *cur_p; 
  
  expire_temp_klines(&temporary_klines);
  for (cur_p = temporary_klines; cur_p; cur_p = cur_p->next)
  {   
    if ((cur_p->user
	 && (!user || match(cur_p->user, user)))
	&& (cur_p->host
	    && (!host || match(cur_p->host,host))))
      return(cur_p);
  }

  expire_temp_klines(&temporary_ip_klines);
  for (cur_p = temporary_ip_klines; cur_p; cur_p = cur_p->next)
  {
    if ((cur_p->user
	 && (!user || match(cur_p->user, user)))
	&& (cur_p->ip
	    && ((ip & cur_p->ip_mask) == cur_p->ip)))
      return(cur_p);
  }
  return (NULL);
}

/*
 * find_is_klined()
 *
 * inputs        - hostname
 *               - username
 *               - ip of possible "victim"
 * output        - matching struct ConfItem or NULL
 * side effects        -
 *
 * WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

struct ConfItem *
find_is_klined (const char* host, const char* name, unsigned long ip)
{
  struct ConfItem *found_aconf;

  if( (found_aconf = find_tkline(host, name, ntohl(ip))) )
    return(found_aconf);

  /* find_matching_mtrie_conf() can return either CONF_KILL,
   * CONF_CLIENT or NULL, i.e. no I line at all.
   */

  found_aconf = find_matching_mtrie_conf(host, name, ntohl(ip));
  if(found_aconf && (found_aconf->status & (CONF_ELINE|CONF_DLINE|CONF_KILL)))
    return(found_aconf);
  else
    return NULL;
}

/*
 * is_klined()
 *
 * inputs	- hostname
 *		- username
 *		- ip of possible victim
 * output	- matching struct or NULL
 * side effects - NONE
 */
struct ConfItem *
is_klined(const char *host,const char *name, unsigned long ip)
{
  struct ConfItem *found_aconf;

  if( (found_aconf = find_tkline(host, name, ip)) )
    return(found_aconf);

  found_aconf = find_matching_mtrie_conf(host, name, ip);
  
  if(found_aconf && (found_aconf->status & CONF_KILL))
    return(found_aconf);
  else
    return NULL;
}
  
/* add_temp_kline
 *
 * inputs        - pointer to struct ConfItem
 * output        - none
 * Side effects  - adds given tkline into temporary kline link list
 * 
 * -Dianora
 */

void
add_temp_kline(struct ConfItem *aconf)
{
  if (aconf->ip == 0)
    {
      aconf->next = temporary_klines;
      temporary_klines = aconf;
    }
  else
    {
      aconf->next = temporary_ip_klines;
      temporary_ip_klines = aconf;
    }
}

/* flush_temp_klines
 *
 * inputs        - NONE
 * output        - NONE
 * side effects        - All temporary klines are flushed out. 
 *
 */
void
flush_temp_klines()
{
  struct ConfItem *kill_list_ptr;

  if( (kill_list_ptr = temporary_klines) )
    {
      while(kill_list_ptr)
        {
          temporary_klines = kill_list_ptr->next;
          free_conf(kill_list_ptr);
          kill_list_ptr = temporary_klines;
        }
    }

  if( (kill_list_ptr = temporary_ip_klines) )
    {
      while(kill_list_ptr)
        {
          temporary_ip_klines = kill_list_ptr->next;
          free_conf(kill_list_ptr);
          kill_list_ptr = temporary_ip_klines;
        }
    }
}

/* report_temp_klines
 *
 * inputs        - aClient pointer, client to report to
 * output        - NONE
 * side effects        - NONE
 *                  
 */
void report_temp_klines(aClient *sptr)
{

  expire_temp_klines(&temporary_klines);
  if(temporary_klines)
      show_temp_klines(sptr, temporary_klines);
  expire_temp_klines(&temporary_ip_klines);
  if(temporary_ip_klines)
      show_temp_klines(sptr, temporary_ip_klines);

}

/* show_temp_klines
 *
 * inputs	- aClient pointer, client to report to
 *		- ConfItem pointer, the tkline list to show
 * outputs	- NONE
 * side effects	- NONE
 */
void
show_temp_klines(struct Client *sptr, struct ConfItem * tklist)
{
  struct ConfItem *cur_p;
  char *host;
  char *user;
  char *reason;

  for(cur_p = tklist; cur_p; cur_p = cur_p->next)
  {
    if(cur_p->host != NULL)
      host = cur_p->host;
    else
      host = "*";

    if(cur_p->user != NULL)
      user = cur_p->user;
    else
      user = "*";

    if(cur_p->passwd)
      reason = cur_p->passwd;
    else
      reason = "No Reason";

    sendto_one(sptr,form_str(RPL_STATSKLINE), me.name,
	       sptr->name, 'k' , host, user, reason);
  }
}

/* oper_privs_from_string
 *
 * inputs        - default privs
 *               - privs as string
 * output        - default privs as modified by privs string
 * side effects -
 *
 */

static int 
oper_privs_from_string(int int_privs,char *privs)
{
  while(*privs)
    {
      if(*privs == 'O')                     /* allow global kill */
        int_privs |= CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'o')                /* disallow global kill */
        int_privs &= ~CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'U')                /* allow unkline */
        int_privs |= CONF_OPER_UNKLINE;
      else if(*privs == 'u')                /* disallow unkline */
        int_privs &= ~CONF_OPER_UNKLINE;
      else if(*privs == 'R')                /* allow remote squit/connect etc.*/
        int_privs |= CONF_OPER_REMOTE;        
      else if(*privs == 'r')                /* disallow remote squit/connect etc.*/
        int_privs &= ~CONF_OPER_REMOTE;
      else if(*privs == 'N')                /* allow +n see nick changes */
        int_privs |= CONF_OPER_N;
      else if(*privs == 'n')                /* disallow +n see nick changes */
        int_privs &= ~CONF_OPER_N;
      else if(*privs == 'K')                /* allow kill and kline privs */
        int_privs |= CONF_OPER_K;
      else if(*privs == 'k')                /* disallow kill and kline privs */
        int_privs &= ~CONF_OPER_K;
#ifdef GLINES
      else if(*privs == 'G')                /* allow gline */
        int_privs |= CONF_OPER_GLINE;
      else if(*privs == 'g')                /* disallow gline */
        int_privs &= ~CONF_OPER_GLINE;
#endif
      else if(*privs == 'H')                /* allow rehash */
        int_privs |= CONF_OPER_REHASH;
      else if(*privs == 'h')                /* disallow rehash */
        int_privs &= ~CONF_OPER_REHASH;
      else if(*privs == 'D')
        int_privs |= CONF_OPER_DIE;         /* allow die */
      else if(*privs == 'd')
        int_privs &= ~CONF_OPER_DIE;        /* disallow die */
      privs++;
    }
  return(int_privs);
}

/*
 * oper_privs_as_string
 *
 * inputs        - pointer to cptr or NULL
 * output        - pointer to static string showing oper privs
 * side effects  -
 * return as string, the oper privs as derived from port
 * also, set the oper privs if given cptr non NULL
 */

char *oper_privs_as_string(aClient *cptr,int port)
{
  static char privs_out[16];
  char *privs_ptr;

  privs_ptr = privs_out;
  *privs_ptr = '\0';

  if(port & CONF_OPER_GLINE)
    {
      if(cptr)
        SetOperGline(cptr);
      *privs_ptr++ = 'G';
    }
  else
    *privs_ptr++ = 'g';

  if(port & CONF_OPER_K)
    {
      if(cptr)
        SetOperK(cptr);
      *privs_ptr++ = 'K';
    }
  else
    *privs_ptr++ = 'k';

  if(port & CONF_OPER_N)
    {
      if(cptr)
        SetOperN(cptr);
      *privs_ptr++ = 'N';
    }
  else
    *privs_ptr++ = 'n';

  if(port & CONF_OPER_GLOBAL_KILL)
    {
      if(cptr)
        SetOperGlobalKill(cptr);
      *privs_ptr++ = 'O';
    }
  else
    *privs_ptr++ = 'o';

  if(port & CONF_OPER_REMOTE)
    {
      if(cptr)
        SetOperRemote(cptr);
      *privs_ptr++ = 'R';
    }
  else
    *privs_ptr++ = 'r';
  
  if(port & CONF_OPER_UNKLINE)
    {
      if(cptr)
        SetOperUnkline(cptr);
      *privs_ptr++ = 'U';
    }
  else
    *privs_ptr++ = 'u';

  if(port & CONF_OPER_REHASH)
    {
      if(cptr)
        SetOperRehash(cptr);
      *privs_ptr++ = 'H';
    }
  else
    *privs_ptr++ = 'h';

  if(port & CONF_OPER_DIE)
    {
      if(cptr)
        SetOperDie(cptr);
      *privs_ptr++ = 'D';
    }
  else
    *privs_ptr++ = 'd';

  *privs_ptr = '\0';

  return(privs_out);
}

/* oper_flags_from_string
 *
 * inputs        - flags as string
 * output        - flags as bit mask
 * side effects -
 *
 * -Dianora
 */

static int 
oper_flags_from_string(char *flags)
{
  int int_flags=0;

  while(*flags)
    {
      if(*flags == 'i')                        /* invisible */
        int_flags |= FLAGS_INVISIBLE;
      else if(*flags == 'w')                /* see wallops */
        int_flags |= FLAGS_WALLOP;
      else if(*flags == 's')
        int_flags |= FLAGS_SERVNOTICE;
      else if(*flags == 'c')
        int_flags |= FLAGS_CCONN;
#ifdef SERVICES
      else if(*flags == 'r')
        int_flags |= FLAGS_NICKREG;
#endif /* SERVICES */
      else if(*flags == 'k')
        int_flags |= FLAGS_SKILL;
      else if(*flags == 'f')
        int_flags |= FLAGS_FULL;
      else if(*flags == 'y')
        int_flags |= FLAGS_SPY;
      else if(*flags == 'd')
        int_flags |= FLAGS_DEBUG;
      else if(*flags == 'n')
        int_flags |= FLAGS_NCHANGE;
      else if(*flags == 'b')
        int_flags |= FLAGS_BOTS;
      else if(*flags == 'x')
        int_flags |= FLAGS_EXTERNAL;
      else if(*flags == 'z')
        int_flags |= FLAGS_OPERWALL;
      flags++;
    }

  return(int_flags);
}

/* oper_flags_as_string
 *
 * inputs        - oper flags as bit mask
 * output        - oper flags as as string
 * side effects -
 *
 * -Dianora
 */

char *oper_flags_as_string(int flags)
{
  static char flags_out[16];
  char *flags_ptr;

  flags_ptr = flags_out;
  *flags_ptr = '\0';

  if(flags & FLAGS_INVISIBLE)
    *flags_ptr++ = 'i';
  if(flags & FLAGS_WALLOP)
    *flags_ptr++ = 'w';
  if(flags & FLAGS_SERVNOTICE)
    *flags_ptr++ = 's';
  if(flags & FLAGS_CCONN)
    *flags_ptr++ = 'c';
#ifdef SERVICES
  if(flags & FLAGS_NICKREG)
    *flags_ptr++ = 'r';
#endif /* SERVICES */
  if(flags & FLAGS_SKILL)
    *flags_ptr++ = 'k';
  if(flags & FLAGS_FULL)
    *flags_ptr++ = 'f';
  if(flags & FLAGS_SPY)
    *flags_ptr++ = 'y';
  if(flags & FLAGS_DEBUG)
    *flags_ptr++ = 'd';
  if(flags & FLAGS_NCHANGE)
    *flags_ptr++ = 'n';
  if(flags & FLAGS_BOTS)
    *flags_ptr++ = 'b';
  if(flags & FLAGS_EXTERNAL)
    *flags_ptr++ = 'x';
  if(flags & FLAGS_OPERWALL)
    *flags_ptr++ = 'z';
  *flags_ptr = '\0';

  return(flags_out);
}

/* table used for is_address */
static unsigned long cidr_to_bitmask[]=
{
  /* 00 */ 0x00000000,
  /* 01 */ 0x80000000,
  /* 02 */ 0xC0000000,
  /* 03 */ 0xE0000000,
  /* 04 */ 0xF0000000,
  /* 05 */ 0xF8000000,
  /* 06 */ 0xFC000000,
  /* 07 */ 0xFE000000,
  /* 08 */ 0xFF000000,
  /* 09 */ 0xFF800000,
  /* 10 */ 0xFFC00000,
  /* 11 */ 0xFFE00000,
  /* 12 */ 0xFFF00000,
  /* 13 */ 0xFFF80000,
  /* 14 */ 0xFFFC0000,
  /* 15 */ 0xFFFE0000,
  /* 16 */ 0xFFFF0000,
  /* 17 */ 0xFFFF8000,
  /* 18 */ 0xFFFFC000,
  /* 19 */ 0xFFFFE000,
  /* 20 */ 0xFFFFF000,
  /* 21 */ 0xFFFFF800,
  /* 22 */ 0xFFFFFC00,
  /* 23 */ 0xFFFFFE00,
  /* 24 */ 0xFFFFFF00,
  /* 25 */ 0xFFFFFF80,
  /* 26 */ 0xFFFFFFC0,
  /* 27 */ 0xFFFFFFE0,
  /* 28 */ 0xFFFFFFF0,
  /* 29 */ 0xFFFFFFF8,
  /* 30 */ 0xFFFFFFFC,
  /* 31 */ 0xFFFFFFFE,
  /* 32 */ 0xFFFFFFFF
};

/*
 * is_address
 *
 * inputs        - hostname
 *                - pointer to ip result
 *                - pointer to ip_mask result
 * output        - YES if hostname is ip# only NO if its not
 *              - 
 * side effects        - NONE
 * 
 * Thanks Soleil
 *
 * BUGS
 */

int        is_address(char *host,
                   unsigned long *ip_ptr,
                   unsigned long *ip_mask_ptr)
{
  unsigned long current_ip=0L;
  unsigned int octet=0;
  int found_mask=0;
  int dot_count=0;
  char c;

  while( (c = *host) )
    {
      if(IsDigit(c))
        {
          octet *= 10;
          octet += (*host & 0xF);
        }
      else if(c == '.')
        {
          current_ip <<= 8;
          current_ip += octet;
          if( octet > 255 )
            return( 0 );
          octet = 0;
          dot_count++;
        }
      else if(c == '/')
        {
          if( octet > 255 )
            return( 0 );
          found_mask = 1;
          current_ip <<= 8;
          current_ip += octet;
          octet = 0;
          *ip_ptr = current_ip;
          current_ip = 0L;
        }
      else if(c == '*')
        {
          if( (dot_count == 3) && (*(host+1) == '\0') && (*(host-1) == '.'))
            {
              current_ip <<= 8;
              *ip_ptr = current_ip;
              *ip_mask_ptr = 0xFFFFFF00L;
              return( 1 );
            }
          else
            return( 0 );
        }
      else
        return( 0 );
      host++;
    }

  current_ip <<= 8;
  current_ip += octet;

  if(found_mask)
    {
      if(current_ip>32)
        return( 0 );
      *ip_mask_ptr = cidr_to_bitmask[current_ip];
    }
  else
    {
      *ip_ptr = current_ip;
      *ip_mask_ptr = 0xFFFFFFFFL;
    }

  return( 1 );
}

#ifdef KILL_COMMENT_IS_FILE
/*
**  output the reason for being k lined from a file
** sptr is server
** parv is the sender prefix
** filename is the file that is to be output to the K lined client
*/
int     m_killcomment(sptr, parv, filename)
aClient *sptr;
char    *parv, *filename;
{
  FBFILE* file;
  char    line[256];
  char    *tmp;
  struct  stat        sb;
  struct  tm        *tm;

  if ((file = fbopen(filename, "r")) == NULL)
    {
      sendto_one(sptr, ":%s %d %s :You are not welcome on this server.", me.name, ERR_YOUREBANNEDCREEP, parv);
      return 0;
    }
  (void)fbstat(&sb, file);
  tm = localtime(&sb.st_mtime);
  while (fbgets(line, sizeof(line), file))
    {
      if ((tmp = strchr(line,'\n')))
        *tmp = '\0';
      sendto_one(sptr, ":%s %d %s :%s.", me.name, ERR_YOUREBANNEDCREEP, parv,line);
    }
  fbclose(file);
  return 0;
}
#endif /* KILL_COMMENT_IS_FILE */


/*
 * m_testline
 *
 * inputs       - pointer to physical connection request is coming from
 *              - pointer to source connection request is comming from
 *              - parc arg count
 *              - parv actual arguments
 *
 * side effects - command to test I/K lines on server
 *
 * i.e. /quote testline user@host,ip
 *
 */

int m_testline(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  struct ConfItem *aconf;
  unsigned long ip;
  unsigned long host_mask;
  char *host, *pass, *user, *name, *given_host, *given_name, *p;
  int port;

  if (!MyClient(sptr) || !IsEmpowered(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      given_name = parv[1];
      if(!(p = strchr(given_name,'@')))
        {
          ip = 0L;
          if(is_address(given_name,&ip,&host_mask))
            {
              aconf = match_Dline(ip);
              if( aconf )
                {
                  get_printable_conf(aconf, &name, &host, &pass, &user, &port);
                  sendto_one(sptr, 
                         ":%s NOTICE %s :D-line host [%s] pass [%s]",
                         me.name, parv[0], 
                         host,
                         pass);
                }
              else
                sendto_one(sptr, ":%s NOTICE %s :No aconf found",
                         me.name, parv[0]);
            }
          else	  
          sendto_one(sptr, ":%s NOTICE %s :usage: user@host|ip",
                     me.name, parv[0]);

          return 0;
        }

      *p = '\0';
      p++;
      given_host = p;
      ip = 0L;
      (void)is_address(given_host,&ip,&host_mask);

      aconf = find_matching_mtrie_conf(given_host,given_name,(unsigned long)ip);

      if(aconf != NULL) 
        {
          get_printable_conf(aconf, &name, &host, &pass, &user, &port);
      
          if(aconf->status & CONF_KILL) 
            {
              sendto_one(sptr, 
                         ":%s NOTICE %s :K-line name [%s] host [%s] pass [%s]",
                         me.name, parv[0], 
                         user,
                         host,
                         pass);
            }
          else if(aconf->status & CONF_CLIENT)
            {
              sendto_one(sptr,
":%s NOTICE %s :I-line mask [%s] prefix [%s] name [%s] host [%s] port [%d] class [%d]",
                         me.name, parv[0], 
                         name,
                         show_iline_prefix(sptr,aconf,user),
                         user,
                         host,
                         port,
                         get_conf_class(aconf));

              aconf = find_tkline(given_host,given_name,0);
              if(aconf)
                {
                  sendto_one(sptr, 
                     ":%s NOTICE %s :k-line name [%s] host [%s] pass [%s]",
                             me.name, parv[0], 
                             aconf->user,
                             aconf->host,
                             aconf->passwd);
                }
            }
        }
      else
        sendto_one(sptr, ":%s NOTICE %s :No aconf found",
                   me.name, parv[0]);
    }
  else
    sendto_one(sptr, ":%s NOTICE %s :usage: user@host|ip",
               me.name, parv[0]);
  return 0;
}


/*
 * get_printable_conf
 *
 * inputs        - struct ConfItem
 *
 * output       - name 
 *              - host
 *              - pass
 *              - oper reason
 *              - user
 *              - port
 *
 * side effects        -
 * Examine the struct struct ConfItem, setting the values
 * of name, host, pass, user to values either
 * in aconf, or "<NULL>" port is set to aconf->port in all cases.
 */

void get_printable_conf(struct ConfItem *aconf, char **name, char **host,
                        char **pass, char **user,int *port)
{
  static  char        null[] = "<NULL>";

  *name = BadPtr(aconf->name) ? null : aconf->name;
  *host = BadPtr(aconf->host) ? null : aconf->host;
  *pass = BadPtr(aconf->passwd) ? null : aconf->passwd;
  *user = BadPtr(aconf->user) ? null : aconf->user;
  *port = (int)aconf->port;
}

/*
 * read_conf_files
 *
 * inputs       - cold start YES or NO
 * output       - none
 * side effects - read all conf files needed, ircd.conf kline.conf etc.
 */

void read_conf_files(int cold)
{
  FBFILE* file = 0;     /* initconf */
  const char *filename,*kfilename,*dfilename; /* conf filenames */

  filename = get_conf_name(CONF_TYPE);

  if ((file = openconf(filename)) == 0)
    {
      if(cold)
        {
          Log("Failed in reading configuration file %s", filename);
          exit(-1);
        }
      else
        {
          sendto_realops("Can't open %s file aborting rehash!", filename );
          return;
        }
    }

  if(!cold)
  {
#ifdef JUPE_CHANNEL
    clear_juped_channels();
#endif /* JUPE_CHANNEL */
    clear_out_old_conf();
  }

  initconf(file, YES);

  do_include_conf();

  /* ZZZ have to deal with single ircd.conf situations */
  /* It would be better to check for NULL return from filename 
   * or *filename == '\0'; and then just ignoring it 
   */

  kfilename = get_conf_name(KLINE_TYPE);
  if (irccmp(filename,kfilename) != 0)
    {
      if ((file = openconf(kfilename)) == 0)
        {
          if(cold)
            {
              Log("Failed reading kline file %s", kfilename);
            }
          else
            {
              sendto_realops("Can't open %s file klines could be missing!",
                         kfilename);
            }
        }
      else
        initconf(file, NO);
    }

  dfilename = get_conf_name(DLINE_TYPE);
  if ((irccmp(filename,dfilename) != 0) && (irccmp(kfilename,dfilename) !=0))
    {
      if ((file = openconf(dfilename)) == 0)
        {
          if(cold)
            {
              Log("Failed reading dline file %s", dfilename);
            }
          else
            {
              sendto_realops("Can't open %s file dlines could be missing!",
                         dfilename);
            }
        }
      else
        initconf(file, NO);
    }

}

/*
 * clear_out_old_conf
 *
 * inputs       - none
 * output       - none
 * side effects - Clear out the old configuration
 */

static void clear_out_old_conf(void)
{
  struct ConfItem **tmp = &ConfigItemList;
  struct ConfItem *tmp2;
  aClass    *cltmp;

    while ((tmp2 = *tmp))
      {
        if (tmp2->clients)
          {
            /*
            ** Configuration entry is still in use by some
            ** local clients, cannot delete it--mark it so
            ** that it will be deleted when the last client
            ** exits...
            */
            if (!(tmp2->status & CONF_CLIENT))
              {
                *tmp = tmp2->next;
                tmp2->next = NULL;
              }
            else
              tmp = &tmp2->next;
            tmp2->status |= CONF_ILLEGAL;
          }
        else
          {
            *tmp = tmp2->next;
            free_conf(tmp2);
          }
      }

    /*
     * We don't delete the class table, rather mark all entries
     * for deletion. The table is cleaned up by check_class. - avalon
     */
    assert(0 != ClassList);
    for (cltmp = ClassList->next; cltmp; cltmp = cltmp->next)
      MaxLinks(cltmp) = -1;

    clear_mtrie_conf_links();

    zap_Dlines();
    clear_special_conf(&x_conf);
    clear_special_conf(&u_conf);
    clear_q_lines();
    mark_listeners_closing();
}

/*
 * flush_deleted_I_P
 *
 * inputs       - none
 * output       - none
 * side effects - This function removes I/P conf items
 */

static void flush_deleted_I_P(void)
{
  struct ConfItem **tmp = &ConfigItemList;
  struct ConfItem *tmp2;

  /*
   * flush out deleted I and P lines although still in use.
   */
  for (tmp = &ConfigItemList; (tmp2 = *tmp); )
    {
      if (!(tmp2->status & CONF_ILLEGAL))
        tmp = &tmp2->next;
      else
        {
          *tmp = tmp2->next;
          tmp2->next = NULL;
          if (!tmp2->clients)
            free_conf(tmp2);
        }
    }
}

/*
 * write_kline_or_dline_to_conf_and_notice_opers
 *
 * inputs       - kline or dline type flag
 *              - client pointer to report to
 *              - server pointer to relay onto
 *              - user name of target
 *              - host name of target
 *              - reason for target
 *              - current tiny date string
 * output       - -1 if error on write, 0 if ok
 * side effects - This function takes care of
 *                finding right kline or dline conf file, writing
 *                the right lines to this file, 
 *                notifying the oper that their kline/dline is in place
 *                notifying the opers on the server about the k/d line
 *                forwarding the kline onto the next U lined server
 *                
 * Bugs         - This function is still doing too much
 */

void write_kline_or_dline_to_conf_and_notice_opers(
                                                   KlineType type,
                                                   aClient *sptr,
                                                   aClient *rcptr,
                                                   char *user,
                                                   char *host,
                                                   char *reason,
                                                   char *current_date)
  {
  char buffer[1024];
  int out;
  const char *filename;         /* filename to use for kline */

  filename = get_conf_name(type);

#ifdef SLAVE_SERVERS
  if(!IsServer(sptr))
#endif
    {
      if(type == DLINE_TYPE)
        {
          sendto_realops("%s added D-Line for [%s] [%s]",
                         sptr->name, host, reason);
          sendto_one(sptr, ":%s NOTICE %s :Added D-Line [%s] to %s",
                     me.name, sptr->name, host, filename);
        }
      else
        {
          sendto_realops("%s added K-Line for [%s@%s] [%s]",
                         sptr->name, user, host, reason);
          sendto_one(sptr, ":%s NOTICE %s :Added K-Line [%s@%s] to %s",
                     me.name, sptr->name, user, host, filename);
        }
    }

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT,0644))==-1)
    {
      sendto_realops("Problem opening %s ", filename);
      return;
    }

  fchmod(out, 0660);

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      if((type==KLINE_TYPE) && rcptr)
        ircsprintf(buffer, "#%s!%s@%s from %s K'd: %s@%s:%s\n",
                   rcptr->name, rcptr->username, rcptr->host,
                   sptr->name,
                   user, host, reason);
    }
  else
#endif
    {
      if(type==KLINE_TYPE)
        ircsprintf(buffer, "#%s!%s@%s K'd: %s@%s:%s\n",
                   sptr->name, sptr->username, sptr->host,
                   user, host, reason);
      else
        ircsprintf(buffer, "#%s!%s@%s D'd: %s:%s\n",
                   sptr->name, sptr->username, sptr->host,
                   host, reason);
    }
  
  if (safe_write(sptr,filename,out,buffer))
    return;

  if(type==KLINE_TYPE)
    ircsprintf(buffer, "K:%s:%s (%s):%s\n",
               host,
               reason,
               current_date,
               user);
  else
    ircsprintf(buffer, "D:%s:%s (%s)\n",
               host,
               reason,
               current_date);


  if (safe_write(sptr,filename,out,buffer))
    return;
      
  close(out);

  if(type==KLINE_TYPE)
    Log("%s added K-Line for [%s@%s] [%s]",
        sptr->name, user, host, reason);
  else
    Log("%s added D-Line for [%s] [%s]",
           sptr->name, host, reason);
}

/*
 * safe_write - write string to file, if an error occurs close the file
 * and notify opers
 *
 * inputs       - client pointer
 *              - filename to write to
 *              - open fd to write on
 *              - buffer to write
 * output       - -1 if error on write, 0 if ok
 * side effects - function tries to write buffer safely
 *                i.e. checking for disk full errors etc.
 */
       
int safe_write(aClient *sptr, const char *filename, int out, char *buffer)
{
  if (write(out, buffer, strlen(buffer)) <= 0)
    {
      sendto_realops("*** Problem writing to %s",filename);
      close(out);
      return -1;
    }
  return 0;
}

/* get_conf_name
 *
 * inputs       - type of conf file to return name of file for
 * output       - pointer to filename for type of conf
 * side effects - none
 */

const char *
get_conf_name(KlineType type)
{
  if(type == CONF_TYPE)
    {
      return(ConfigFileEntry.configfile);
    }
  else if(type == KLINE_TYPE)
    {
      return(ConfigFileEntry.klinefile);
    }

  return(ConfigFileEntry.dlinefile);
}

/*
 * field breakup for ircd.conf file.
 */
static char *getfield(char *newline)
{
  static char *line = (char *)NULL;
  char  *end, *field;
        
  if (newline)
    line = newline;

  if (line == (char *)NULL)
    return((char *)NULL);

  field = line;
  if ((end = strchr(line,':')) == NULL)
    {
      line = (char *)NULL;
      if ((end = strchr(field,'\n')) == (char *)NULL)
        end = field + strlen(field);
    }
  else
    line = end + 1;
  *end = '\0';
  return(field);
}
