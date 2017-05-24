/************************************************************************
 *   IRC - Internet Relay Chat, src/s_log.c
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
 *   $Id: s_log.c,v 1.1.1.1 2006/03/08 23:28:12 malign Exp $
 */

#include "s_log.h"
#include "irc_string.h"
#include "ircd.h"
#include "s_misc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOG_BUFSIZE 2000 

#ifdef LOGGING
static int logFile = -1;
#endif /* LOGGING */

/*
 * open_log - open ircd logging file
 * returns true (1) if successful, false (0) otherwise
 */
#if defined(LOGGING) 
static int open_log(const char* filename)
{
  logFile = open(filename, 
                 O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, 0644);
  if (-1 == logFile) {
    return 0;
  }
  return 1;
}
#endif

void close_log(void)
{
#if defined(LOGGING) 
  if (-1 < logFile) {
    close(logFile);
    logFile = -1;
  }
#endif
}

#if defined(LOGGING) 
static void write_log(const char* message)
{
  char buf[LOG_BUFSIZE];
  sprintf(buf, "[%s] %s\n", smalldate(CurrentTime), message);
  write(logFile, buf, strlen(buf));
}
#endif
   
void Log(const char* fmt, ...)
{
  char    buf[LOG_BUFSIZE];
  va_list args;
  assert(0 != fmt);

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

#if defined(LOGGING) 
  write_log(buf);
#endif
}
  
void init_log(const char* filename)
{
#if defined(LOGGING) 
  open_log(filename);
#endif
}
