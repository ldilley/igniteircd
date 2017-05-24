#ifndef INCLUDED_s_conf_h
#define INCLUDED_s_conf_h
/************************************************************************
 *   IRC - Internet Relay Chat, include/s_conf.h
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
 *   $Id: s_conf.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */

#ifndef INCLUDED_config_h
#include "config.h"             /* defines */
#endif
#ifndef INCLUDED_fileio_h
#include "fileio.h"             /* FBFILE */
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>         /* in_addr */
#define INCLUDED_netinet_in_h
#endif
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"
#endif
#ifndef INCLUDED_motd_h
#include "motd.h"               /* MessageFile */
#endif

struct Client;
struct SLink;
struct DNSReply;
struct hostent;

struct ConfItem
{
  struct ConfItem* next;     /* list node pointer */
  unsigned int     status;   /* If CONF_ILLEGAL, delete when no clients */
  unsigned int     flags;
  int              clients;  /* Number of *LOCAL* clients using this */
  struct in_addr   ipnum;    /* ip number of host field */
  unsigned long    ip;       /* only used for I D lines etc. */
  unsigned long    ip_mask;
  char*            name;     /* IRC name, nick, server name, or original u@h */
  char*            host;     /* host part of user@host */
  char*            passwd;   /* doubles as kline reason */
  char*            user;     /* user part of user@host */
  int              port;
  time_t           hold;     /* Hold action until this time (calendar time) */
  struct Class*    c_class;     /* Class of connection */
  int              dns_pending; /* 1 if dns query pending, 0 otherwise */
  struct DNSQuery* dns_query;
};

typedef struct QlineItem {
  char      *name;
  struct    ConfItem *confList;
  struct    QlineItem *next;
}aQlineItem;

#define CONF_ILLEGAL            0x80000000
#define CONF_MATCH              0x40000000
#define CONF_QUARANTINED_NICK   0x0001
#define CONF_CLIENT             0x0002
#define CONF_CONNECT_SERVER     0x0004
#define CONF_NOCONNECT_SERVER   0x0008
#define CONF_OPER               0x0010
#define CONF_ADMINISTRATOR      0x0020
#define CONF_ME                 0x0040
#define CONF_KILL               0x0080
#define CONF_ADMIN              0x0100
/*
 * R_LINES are no more
 * -wnder
 *
 * #ifdef  R_LINES
 * #define CONF_RESTRICT           0x0200
 * #endif
 */
#define CONF_CLASS              0x0400
#define CONF_LEAF               0x0800
#define CONF_LISTEN_PORT        0x1000
#define CONF_HUB                0x2000
#define CONF_ELINE              0x4000
#define CONF_FLINE              0x8000
#define CONF_DLINE              0x20000
#define CONF_XLINE              0x40000
#define CONF_ULINE              0x80000

#define CONF_OPS                (CONF_ADMINISTRATOR | CONF_OPER)
#define CONF_SERVER_MASK        (CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER)
#define CONF_CLIENT_MASK        (CONF_CLIENT | CONF_OPS | CONF_SERVER_MASK)

#define IsIllegal(x)    ((x)->status & CONF_ILLEGAL)

/* aConfItem->flags */

#define CONF_FLAGS_LIMIT_IP             0x0001
#define CONF_FLAGS_NO_TILDE             0x0002
#define CONF_FLAGS_NEED_IDENTD          0x0004
#define CONF_FLAGS_PASS_IDENTD          0x0008
#define CONF_FLAGS_NOMATCH_IP           0x0010
#define CONF_FLAGS_E_LINED              0x0020
#define CONF_FLAGS_F_LINED              0x0080
#define CONF_FLAGS_EXEMPTGLINE          0x2000

#ifdef IDLE_CHECK
#define CONF_FLAGS_IDLE_LINED           0x0100
#endif

#define CONF_FLAGS_DO_IDENTD            0x0200
#define CONF_FLAGS_ALLOW_AUTO_CONN      0x0400
#define CONF_FLAGS_ZIP_LINK             0x0800
#define CONF_FLAGS_SPOOF_IP             0x1000

#ifdef LITTLE_I_LINES
#define CONF_FLAGS_LITTLE_I_LINE        0x8000
#endif


/* Macros for aConfItem */

#define IsLimitIp(x)            ((x)->flags & CONF_FLAGS_LIMIT_IP)
#define IsNoTilde(x)            ((x)->flags & CONF_FLAGS_NO_TILDE)
#define IsNeedIdentd(x)         ((x)->flags & CONF_FLAGS_NEED_IDENTD)
#define IsPassIdentd(x)         ((x)->flags & CONF_FLAGS_PASS_IDENTD)
#define IsNoMatchIp(x)          ((x)->flags & CONF_FLAGS_NOMATCH_IP)
#define IsConfElined(x)         ((x)->flags & CONF_FLAGS_E_LINED)
#define IsConfFlined(x)         ((x)->flags & CONF_FLAGS_F_LINED)
#define IsConfExemptGline(x)    ((x)->flags & CONF_FLAGS_EXEMPTGLINE)

#ifdef IDLE_CHECK
#define IsConfIdlelined(x)      ((x)->flags & CONF_FLAGS_IDLE_LINED)
#endif

#define IsConfDoIdentd(x)       ((x)->flags & CONF_FLAGS_DO_IDENTD)
#define IsConfDoSpoofIp(x)      ((x)->flags & CONF_FLAGS_SPOOF_IP)
#ifdef LITTLE_I_LINES
#define IsConfLittleI(x)        ((x)->flags & CONF_FLAGS_LITTLE_I_LINE)
#endif

/* port definitions for Opers */

#define CONF_OPER_GLOBAL_KILL 1
#define CONF_OPER_REMOTE      2
#define CONF_OPER_UNKLINE     4
#define CONF_OPER_GLINE       8
#define CONF_OPER_N          16
#define CONF_OPER_K          32
#define CONF_OPER_REHASH     64
#define CONF_OPER_DIE       128

typedef struct
{
  char *dpath;          /* DPATH if set from command line */
  char *configfile;
  char *klinefile;
  char *dlinefile;

#ifdef GLINES
  char  *glinefile;
#endif

  MessageFile helpfile;
  MessageFile motd;
  MessageFile opermotd;
} ConfigFileEntryType;

/* aConfItems */
/* conf uline link list root */
extern struct ConfItem *u_conf;
/* conf xline link list root */
extern struct ConfItem *x_conf;
/* conf qline link list root */
extern struct QlineItem *q_conf;

extern struct ConfItem* ConfigItemList;        /* GLOBAL - conf list head */
extern int              specific_virtual_host; /* GLOBAL - used in s_bsd.c */
extern struct ConfItem *temporary_klines;
extern struct ConfItem *temporary_ip_klines;
extern ConfigFileEntryType ConfigFileEntry;    /* GLOBAL - defined in ircd.c */

extern void clear_ip_hash_table(void);
extern void iphash_stats(struct Client *,struct Client *,int,char **,int);
extern void count_ip_hash(int *,u_long *);

#ifdef LIMIT_UH
void remove_one_ip(struct Client *);
#else
void remove_one_ip(unsigned long);
#endif

extern int   check_client(struct Client*, char *,char **);
extern struct ConfItem* make_conf(void);
extern void             free_conf(struct ConfItem*);

extern void             read_conf_files(int cold);

extern void conf_dns_lookup(struct ConfItem* aconf);
extern int              attach_conf(struct Client*, struct ConfItem *);
extern int              attach_confs(struct Client* client, 
                                     const char* name, int statmask);
extern int              attach_cn_lines(struct Client* client, 
                                        const char *name,
					const char *host);
extern struct ConfItem* find_me(void);
extern struct ConfItem* find_admin(void);
extern struct ConfItem* find_first_nline(struct SLink* lp);
extern void             det_confs_butmask (struct Client *, int);
extern int              detach_conf (struct Client *, struct ConfItem *);
extern struct ConfItem* det_confs_butone (struct Client *, struct ConfItem *);
extern struct ConfItem* find_conf_entry(struct ConfItem *, int);
extern struct ConfItem* find_conf_exact(const char* name, const char* user, 
                                        const char* host, int statmask);
extern struct ConfItem* find_conf_name(struct SLink* lp, const char* name, 
                                       int statmask);
extern struct ConfItem* find_conf_host(struct SLink* lp, const char* host, 
                                       int statmask);
extern struct ConfItem* find_conf_ip(struct SLink* lp, char* ip, char* name, 
                                     int);
extern struct ConfItem* find_conf_by_name(const char* name, int status);
extern struct ConfItem* find_conf_by_host(const char* host, int status);
extern struct ConfItem* find_kill (struct Client *);
extern int conf_connect_allowed(struct in_addr addr);
extern char *oper_flags_as_string(int);
extern char *oper_privs_as_string(struct Client *, int);
extern int rehash_dump(struct Client *);
extern int find_q_line(char*, char*, char *);
extern struct ConfItem* find_special_conf(char *,int );
extern struct ConfItem* is_klined(const char *host,
                                  const char *name,
				  unsigned long ip);
extern struct ConfItem* find_is_klined(const char* host, 
                                       const char* name,
                                       unsigned long ip);
extern char* show_iline_prefix(struct Client *,struct ConfItem *,char *);
extern void get_printable_conf(struct ConfItem *,
			       char **, char **, char **, char **, int *);
extern void report_configured_links(struct Client* cptr, int mask);
extern void report_specials(struct Client* sptr, int flags, int numeric);
extern void report_qlines(struct Client* cptr);

extern void clear_juped_channels();

typedef enum {
  CONF_TYPE,
  KLINE_TYPE,
  DLINE_TYPE
} KlineType;

extern void write_kline_or_dline_to_conf_and_notice_opers(
                                                          KlineType,
                                                          struct Client *,
                                                          struct Client *,
                                                          char *,
                                                          char *,
                                                          char *,
                                                          char *
                                                          );
extern const char *get_conf_name(KlineType);
extern int safe_write(struct Client *, const char *, int ,char *);
extern void add_temp_kline(struct ConfItem *);
extern  void    flush_temp_klines(void);
extern  void    report_temp_klines(struct Client *);
extern  void    show_temp_klines(struct Client *, struct ConfItem *);
extern  int     is_address(char *,unsigned long *,unsigned long *); 
extern  int     rehash (struct Client *, struct Client *, int);


#endif /* INCLUDED_s_conf_h */

