/************************************************************************
 *   IRC - Internet Relay Chat, include/packet.h
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
 *
 * "packet.h". - Headers file.
 *
 * $Id: packet.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 *
 */
#ifndef INCLUDED_packet_h
#define INCLUDED_packet_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;

extern int dopacket(struct Client* client, char* buf, size_t len);
extern int client_dopacket(struct Client* client, char* buf, size_t len);

#endif /* INCLUDED_packet_h */


