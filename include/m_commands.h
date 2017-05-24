/************************************************************************
 *   IRC - Internet Relay Chat, include/m_commands.h
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
 * $Id: m_commands.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 */
#ifndef INCLUDED_m_commands_h
#define INCLUDED_m_commands_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

struct Client;

extern int m_testline(struct Client *,struct Client *,int,char **);

extern int m_admin(struct Client *,struct Client *,int,char **);
extern int m_kline(struct Client *,struct Client *,int,char **);
extern int m_unkline(struct Client *,struct Client *,int,char **);
extern int m_dline(struct Client *,struct Client *,int,char **);
extern int m_undline(struct Client *,struct Client *,int,char **);

extern int m_gline(struct Client *,struct Client *,int,char **);
extern int m_ungline(struct Client *,struct Client *,int,char **);

extern int m_locops(struct Client *,struct Client *,int,char **);

extern int m_private(struct Client *,struct Client *,int,char **);
extern int m_knock(struct Client *,struct Client *,int,char **);
extern int m_topic(struct Client *,struct Client *,int,char **);
extern int m_join(struct Client *,struct Client *,int,char **);
extern int m_part(struct Client *,struct Client *,int,char **);
extern int m_mode(struct Client *,struct Client *,int,char **);
extern int m_ping(struct Client *,struct Client *,int,char **);
extern int m_pong(struct Client *,struct Client *,int,char **);
extern int m_wallops(struct Client *,struct Client *,int,char **);
extern int m_kick(struct Client *,struct Client *,int,char **);
extern int m_nick(struct Client *,struct Client *,int,char **);
extern int m_error(struct Client *,struct Client *,int,char **);
extern int m_notice(struct Client *,struct Client *,int,char **);
extern int m_invite(struct Client *,struct Client *,int,char **);
extern int m_quit(struct Client *,struct Client *,int,char **);

extern int m_capab(struct Client *,struct Client *,int,char **);
extern int m_info(struct Client *,struct Client *,int,char **);
extern int m_kill(struct Client *,struct Client *,int,char **);
extern int m_list(struct Client *,struct Client *,int, char **);
extern int m_motd(struct Client *,struct Client *,int,char **);
extern int m_who(struct Client *,struct Client *,int,char **);
extern int m_whois(struct Client *,struct Client *,int,char **);
extern int m_server(struct Client *,struct Client *,int,char **);
extern int m_user(struct Client *,struct Client *,int, char **);
extern int m_links(struct Client *,struct Client *,int,char **);
extern int m_summon(struct Client *,struct Client *,int,char **);
extern int m_stats(struct Client *,struct Client *,int,char **);
extern int m_users(struct Client *,struct Client *,int,char **);
extern int m_version(struct Client *,struct Client *,int, char **);
extern int m_help(struct Client *,struct Client *,int, char**);
extern int m_squit(struct Client *,struct Client *,int, char **);
extern int m_away(struct Client *,struct Client *,int,char **);
extern int m_connect(struct Client *,struct Client *,int,char **);
extern int m_oper(struct Client *,struct Client *,int,char **);
extern int m_pass(struct Client *,struct Client *,int,char **);
extern int m_trace(struct Client *,struct Client *,int,char **);
#ifdef LTRACE
extern int m_ltrace(struct Client *,struct Client *,int,char **);
#endif /* LTRACE */
extern int m_time(struct Client *,struct Client *,int, char **);
extern int m_names(struct Client *,struct Client *,int,char **);

extern int m_lusers(struct Client *,struct Client *,int, char **);
extern int m_close(struct Client *,struct Client *,int,char **);

extern int m_whowas(struct Client *,struct Client *,int,char **);
extern int m_userhost(struct Client *,struct Client *,int,char **);
extern int m_ison(struct Client *,struct Client *,int,char **);
extern int m_svinfo(struct Client *,struct Client *,int,char **);
extern int m_fjoin(struct Client *,struct Client *,int,char **);
extern int m_fnick(struct Client *,struct Client *,int,char **);
extern int m_sjoin(struct Client *,struct Client *,int,char **);
extern int m_operwall(struct Client *,struct Client *,int,char **);
extern int m_rehash(struct Client *,struct Client *,int,char **);
extern int m_restart(struct Client *,struct Client *,int,char **);
extern int m_die(struct Client *,struct Client *,int,char **);
extern int m_hash(struct Client *,struct Client *,int,char **);
extern int m_dns(struct Client *,struct Client *,int,char **);
extern int m_htm(struct Client *,struct Client *,int,char **);
extern int m_set(struct Client *,struct Client *,int,char **);
#endif /* INCLUDED_m_commands_h */
