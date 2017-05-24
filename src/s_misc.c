/************************************************************************
 *   IRC - Internet Relay Chat, src/s_misc.c
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
 *  $Id: s_misc.c,v 1.2 2007/02/14 10:00:44 malign Exp $
 */

#include "s_misc.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "res.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "struct.h"

#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>


static char* months[] = {
  "January",   "February", "March",   "April",
  "May",       "June",     "July",    "August",
  "September", "October",  "November","December"
};

static char* weekdays[] = {
  "Sunday",   "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
};

char* date(time_t clockval) 
{
  static        char        buf[80], plus;
  struct        tm *lt, *gm;
  struct        tm        gmbuf;
  int        minswest;

  if (!clockval) 
    time(&clockval);
  gm = gmtime(&clockval);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf;
  lt = localtime(&clockval);

  if (lt->tm_yday == gm->tm_yday)
    minswest = (gm->tm_hour - lt->tm_hour) * 60 +
      (gm->tm_min - lt->tm_min);
  else if (lt->tm_yday > gm->tm_yday)
    minswest = (gm->tm_hour - (lt->tm_hour + 24)) * 60;
  else
    minswest = ((gm->tm_hour + 24) - lt->tm_hour) * 60;

  plus = (minswest > 0) ? '-' : '+';
  if (minswest < 0)
    minswest = -minswest;
  
  ircsprintf(buf, "%s %s %d %d -- %t:%t:%t %c%t:%t",
          weekdays[lt->tm_wday], months[lt->tm_mon],lt->tm_mday,
          lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec,
          plus, minswest/60, minswest%60);

  return buf;
}

const char* smalldate(time_t clockval)
{
  static  char    buf[MAX_DATE_STRING];
  struct  tm *lt, *gm;
  struct  tm      gmbuf;

  if (!clockval)
    time(&clockval);
  gm = gmtime(&clockval);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf; 
  lt = localtime(&clockval);
  
  ircsprintf(buf, "%t/%t/%d %t:%t",
             lt->tm_mon + 1, lt->tm_mday, lt->tm_year + 1900,
             lt->tm_hour, lt->tm_min);
  
  return buf;
}


#if defined(GLINES)
/*
 * small_file_date
 * Make a small YYYYMMDD formatted string suitable for a
 * dated file stamp. 
 */
char* small_file_date(time_t clockval)
{
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  if (!clockval)
    time(&clockval);
  tmptr = localtime(&clockval);
  strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d", tmptr);
  return timebuffer;
}
#endif

/*
 * Retarded - so sue me :-P
 */
#define _1MEG     (1024.0)
#define _1GIG     (1024.0*1024.0)
#define _1TER     (1024.0*1024.0*1024.0)
#define _GMKs(x)  ( (x > _1TER) ? "Terabytes" : ((x > _1GIG) ? "Gigabytes" : \
                  ((x > _1MEG) ? "Megabytes" : "Kilobytes")))
#define _GMKv(x)  ( (x > _1TER) ? (float)(x/_1TER) : ((x > _1GIG) ? \
               (float)(x/_1GIG) : ((x > _1MEG) ? (float)(x/_1MEG) : (float)x)))

void serv_info(aClient *cptr,char *name)
{
  static char Lformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
  int        j;
  long        sendK, receiveK, uptime;
  aClient        *acptr;

  sendK = receiveK = 0;
  j = 1;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      sendK += acptr->sendK;
      receiveK += acptr->receiveK;
      /* There are no more non TS servers on this network, so that test has
       * been removed. Also, do not allow non opers to see the IP's of servers
       * on stats ?
       */
      if(IsEmpowered(cptr))
        sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO, name,
#if (defined SERVERHIDE) || (defined HIDE_SERVERS_IPS)
                   get_client_name(acptr, HIDEME),
#else
                   get_client_name(acptr, TRUE),
#endif
                   (int)DBufLength(&acptr->sendQ),
                   (int)acptr->sendM, (int)acptr->sendK,
                   (int)acptr->receiveM, (int)acptr->receiveK,
                   CurrentTime - acptr->firsttime,
                   (CurrentTime > acptr->since) ? (CurrentTime - acptr->since): 0,
                   IsServer(acptr) ? show_capabilities(acptr) : "-" );
      else
        {
          sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
                     name, get_client_name(acptr, HIDEME),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     CurrentTime - acptr->firsttime,
                     (CurrentTime > acptr->since)?(CurrentTime - acptr->since): 0,
                     IsServer(acptr) ? show_capabilities(acptr) : "-" );
        }
      j++;
    }

  sendto_one(cptr, ":%s %d %s :%u total server%s",
             /*me.name, RPL_STATSDEBUG, name, --j, (j==1)?"":"s");*/
             me.name, RPL_STATSDEBUG, name, j, (j==1)?"":"s");
  sendto_one(cptr, ":%s %d %s :Sent total : %7.2f %s",
             me.name, RPL_STATSDEBUG, name, _GMKv(sendK), _GMKs(sendK));
  sendto_one(cptr, ":%s %d %s :Recv total : %7.2f %s",
             me.name, RPL_STATSDEBUG, name, _GMKv(receiveK), _GMKs(receiveK));

  uptime = (CurrentTime - me.since);
  sendto_one(cptr, ":%s %d %s :Server send: %7.2f %s (%4.1f K/s)",
             me.name, RPL_STATSDEBUG, name, _GMKv(me.sendK), _GMKs(me.sendK),
             (float)((float)me.sendK / (float)uptime));
  sendto_one(cptr, ":%s %d %s :Server recv: %7.2f %s (%4.1f K/s)",
             me.name, RPL_STATSDEBUG, name, _GMKv(me.receiveK), _GMKs(me.receiveK),
             (float)((float)me.receiveK / (float)uptime));
}

/* Make ISUPPORT string */
char *make_isupport()
{
  static char tisupport[200];

 /* ircsprintf(tisupport, "WALLCHOPS PREFIX=(aov)*@+ CHANTYPES=#& MAXCHANNELS=%d "
                        "MAXBANS=%d NICKLEN=%d TOPICLEN=%d KICKLEN=%d "
                        "NETWORK=%s " */

ircsprintf(tisupport, "WALLCHOPS PREFIX=(ov)@+ CHANTYPES=#& MAXCHANNELS=%d "
                        "MAXBANS=%d NICKLEN=%d TOPICLEN=%d KICKLEN=%d "
                        "NETWORK=%s "
/*
#ifdef CHANMODE_E
                        "CHANMODES=be,k,l,imnpst EXCEPTS"
#else
                        "CHANMODES=b,k,l,imnpst"
#endif */

/*
#ifdef SERVICES
                        "CHANMODES=b,k,l,imnprst"
#else
                        "CHANMODES=b,k,l,imnpst"
#endif */


#if defined(CHANMODE_E) && defined(SERVICES)
                        "CHANMODES=be,k,l,imnprst EXCEPTS"
#elif defined(CHANMODE_E) && !defined(SERVICES)
                        "CHANMODES=be,k,l,imnpst EXCEPTS"
#elif defined(SERVICES) && !defined(CHANMODE_E)
                        "CHANMODES=b,k,l,imnprst"
#else
                        "CHANMODES=b,k,l,imnpst"
#endif /* CHANNEL MODES */

#ifdef USE_KNOCK
                        " KNOCK"
#endif /* USE_KNOCK */

#ifdef SERVICES
                        " REGISTERED"
#endif /* SERVICES */

                        " MODES=%d", MAXCHANNELSPERUSER, MAXBANS, NICKLEN,
                        TOPICLEN, KILLLEN, NETWORK_NAME, MAXMODEPARAMS);

  return tisupport;
}


