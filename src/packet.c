/************************************************************************
 *   IRC - Internet Relay Chat, src/packet.c
 *   Copyright (C) 1990  Jarkko Oikarinen and
 *                       University of Oulu, Computing Center
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
 *   $Id: packet.c,v 1.1.1.1 2006/03/08 23:28:11 malign Exp $
 */ 
#include "packet.h"
#include "client.h"
#include "common.h"
#include "ircd.h"
#include "list.h"
#include "parse.h"
#include "s_zip.h"
#include "struct.h"
#include "irc_string.h"

#include <assert.h>
#include <string.h>

/*
** dopacket
**      cptr - pointer to client structure for which the buffer data
**             applies.
**      buffer - pointr to the buffer containing the newly read data
**      length - number of valid bytes of data in the buffer
**
**      The buffer might be partially or totally zipped.
**      At the beginning of the compressed flow, it is possible that
**      an uncompressed ERROR message will be found.  This occurs when
**      the connection fails on the other server before switching
**      to compressed mode.
**
** Note:
**      It is implicitly assumed that dopacket is called only
**      with cptr of "local" variation, which contains all the
**      necessary fields (buffer etc..)
*/
int dopacket(aClient *cptr, char *buffer, size_t length)
{
  char  *ch1;
  char  *ch2;
  char *cptrbuf;
#ifdef ZIP_LINKS
  int  zipped = NO;
  int  done_unzip = NO;
#endif

  cptrbuf = cptr->buffer;
  me.receiveB += length; /* Update bytes received */
  cptr->receiveB += length;

  if (cptr->receiveB > 1023)
    {
      cptr->receiveK += (cptr->receiveB >> 10);
      cptr->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
    }

  if (me.receiveB > 1023)
    {
      me.receiveK += (me.receiveB >> 10);
      me.receiveB &= 0x03ff;
    }
  ch1 = cptrbuf + cptr->count;
  ch2 = buffer;

#ifdef ZIP_LINKS
  if (cptr->flags2 & FLAGS2_ZIPFIRST)
    {
      if (*ch2 == '\n' || *ch2 == '\r')
        {
          ch2++;
          length--;
        }
      cptr->flags2 &= ~FLAGS2_ZIPFIRST;
    }
  else
    done_unzip = YES;

  if (cptr->flags2 & FLAGS2_ZIP)
    {
      /* uncompressed buffer first */
      zipped = length;
      cptr->zip->inbuf[0] = '\0';    /* unnecessary but nicer for debugging */
      cptr->zip->incount = 0;
      ch2 = unzip_packet(cptr, ch2, &zipped);
      length = zipped;
      zipped = 1;
      if (length == -1)
        return exit_client(cptr, cptr, &me,
                           "fatal error in unzip_packet(1)");
    }
#endif /* ZIP_LINKS */

#ifdef ZIP_LINKS
  /* While there is "stuff" in the compressed input to deal with,
   * keep loop parsing it. I have to go through this loop at least once.
   * -Dianora
   */
  do
    {
#endif
      /* While there is "stuff" in uncompressed input to deal with
       * loop around parsing it. -Dianora
       */
      while (length-- > 0)
        {
          char g;
          g = (*ch1 = *ch2++);
          /*
           * Yuck.  Stuck.  To make sure we stay backward compatible,
           * we must assume that either CR or LF terminates the message
           * and not CR-LF.  By allowing CR or LF (alone) into the body
           * of messages, backward compatibility is lost and major
           * problems will arise. - Avalon
           */

          /* The previous code is just silly, you do at least one test
           * to see if g is less than 16, then at least one more, total of two
           * its gotta be a '\r' or a '\n' before anything happens, so why
           * not just check for either '\n' or '\r' ?
           * -Dianora
           */
          /*      if ( g < '\16' && (g== '\n' || g == '\r')) */

          if ( g == '\n' || g == '\r' )
            {
              if (ch1 == cptrbuf)
                continue; /* Skip extra LF/CR's */
              *ch1 = '\0';
              me.receiveM += 1; /* Update messages received */
              cptr->receiveM += 1;
              cptr->count = 0; /* ...just in case parse returns with
                               ** CLIENT_EXITED without removing the
                               ** structure pointed by cptr... --msa
                               */
              if (parse(cptr, cptr->buffer, ch1) == CLIENT_EXITED)
                /*
                ** CLIENT_EXITED means actually that cptr
                ** structure *does* not exist anymore!!! --msa
                */
                return CLIENT_EXITED;
              /*
              ** Socket is dead so exit (which always returns with
              ** CLIENT_EXITED here).  - avalon
              */
              if (cptr->flags & FLAGS_DEADSOCKET)
                return exit_client(cptr, cptr, &me, (cptr->flags & FLAGS_SENDQEX) ?
                                   ((IsDoingList(cptr)) ?
                                    "Local kill by /list (so many channels!)" :
                                   "SendQ exceeded") : "Dead socket");

#ifdef ZIP_LINKS
              if ((cptr->flags2 & FLAGS2_ZIP) && (zipped == 0) &&
                  (length > 0))
                {
                  /*
                  ** beginning of server connection, the buffer
                  ** contained PASS/CAPAB/SERVER and is now 
                  ** zipped!
                  ** Ignore the '\n' that should be here.
                  */
                  /* Checked RFC1950: \r or \n can't start a
                  ** zlib stream  -orabidoo
                  */

                  zipped = length;
                  if (zipped > 0 && (*ch2 == '\n' || *ch2 == '\r'))
                    {
                      ch2++;
                      zipped--;
                    }
                  cptr->flags2 &= ~FLAGS2_ZIPFIRST;
                  ch2 = unzip_packet(cptr, ch2, &zipped);
                  length = zipped;
                  zipped = 1;
                  if (length == -1)
                    return exit_client(cptr, cptr, &me,
                                       "fatal error in unzip_packet(2)");
                }
#endif /* ZIP_LINKS */
              ch1 = cptrbuf;
            }
          else if (ch1 < cptrbuf + (sizeof(cptr->buffer)-1))
            ch1++; /* There is always room for the null */
        }
#ifdef ZIP_LINKS
      /* Now see if anything is left uncompressed in the input
       * If so, uncompress it and continue to parse
       * -Dianora
       */
          if((cptr->flags2 & FLAGS2_ZIP) && cptr->zip->incount)
            {
              /* This call simply finishes unzipping whats left
               * second parameter is not used. -Dianora
               */
              ch2 = unzip_packet(cptr, (char *)NULL, &zipped);
              length = zipped;
              zipped = 1;
              if (length == -1)
                return exit_client(cptr, cptr, &me,
                                   "fatal error in unzip_packet(1)");
              ch1 = ch2 + length;
              done_unzip = NO;
            }
          else
            done_unzip = YES;
#endif

#ifdef ZIP_LINKS
    }while(!done_unzip);
#endif
  cptr->count = ch1 - cptrbuf;
  return 1;
}


/*
 * client_dopacket - copy packet to client buf and parse it
 *      cptr - pointer to client structure for which the buffer data
 *             applies.
 *      buffer - pointr to the buffer containing the newly read data
 *      length - number of valid bytes of data in the buffer
 *
 *      The buffer might be partially or totally zipped.
 *      At the beginning of the compressed flow, it is possible that
 *      an uncompressed ERROR message will be found.  This occurs when
 *      the connection fails on the other server before switching
 *      to compressed mode.
 *
 * Note:
 *      It is implicitly assumed that dopacket is called only
 *      with cptr of "local" variation, which contains all the
 *      necessary fields (buffer etc..)
 */
int client_dopacket(struct Client *cptr, char *buffer, size_t length)
{
  assert(0 != cptr);
  assert(0 != buffer);

  strncpy_irc(cptr->buffer, buffer, BUFSIZE);
  length = strlen(cptr->buffer); 

  /* 
   * Update messages received
   */
  ++me.receiveM;
  ++cptr->receiveM;

  /* 
   * Update bytes received
   */
  cptr->receiveB += length;

  if (cptr->receiveB > 1023) {
    cptr->receiveK += (cptr->receiveB >> 10);
    cptr->receiveB &= 0x03ff; /* 2^10 = 1024, 3ff = 1023 */
  }

  me.receiveB += length;

  if (me.receiveB > 1023) {
    me.receiveK += (me.receiveB >> 10);
    me.receiveB &= 0x03ff;
  }

  cptr->count = 0;    /* ...just in case parse returns with */
  if (CLIENT_EXITED == parse(cptr, cptr->buffer, cptr->buffer + length)) {
    /*
     * CLIENT_EXITED means actually that cptr
     * structure *does* not exist anymore!!! --msa
     */
    return CLIENT_EXITED;
  }
  else if (cptr->flags & FLAGS_DEADSOCKET) {
    /*
     * Socket is dead so exit (which always returns with
     * CLIENT_EXITED here).  - avalon
     */
    return exit_client(cptr, cptr, &me, 
            (cptr->flags & FLAGS_SENDQEX) ? "SendQ exceeded" : "Dead socket");
  }
  return 1;
}
