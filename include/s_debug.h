/************************************************************************
 *   IRC - Internet Relay Chat, include/s_debug.h
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
 *   $Id: s_debug.h,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $
 */
#ifndef INCLUDED_s_debug_h
#define INCLUDED_s_debug_h

#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* size_t */
#define INCLUDED_sys_types_h
#endif

struct Client;

/*
 * defined debugging levels
 */
#define DEBUG_FATAL  0
#define DEBUG_ERROR  1  /* report_error() and other errors that are found */
#define DEBUG_NOTICE 3
#define DEBUG_DNS    4  /* used by all DNS related routines - a *lot* */
#define DEBUG_INFO   5  /* general usful info */
#define DEBUG_NUM    6  /* numerics */
#define DEBUG_SEND   7  /* everything that is sent out */
#define DEBUG_DEBUG  8  /* anything to do with debugging, ie unimportant :) */
#define DEBUG_MALLOC 9  /* malloc/free calls */
#define DEBUG_LIST  10  /* debug list use */


extern void send_usage(struct Client*, char *);
extern void count_memory (struct Client *, char *);

extern void debug(int, char *, ...);

#endif /* INCLUDED_s_debug_h */

