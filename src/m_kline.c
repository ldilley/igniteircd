/************************************************************************
 *   IRC - Internet Relay Chat, src/m_kline.c
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
 *   $Id: m_kline.c,v 1.1.1.1 2006/03/08 23:28:10 malign Exp $
 */

#include "m_commands.h"
#include "m_kline.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "irc_string.h"
#include "ircd.h"
#include "mtrie_conf.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_misc.h"
#include "send.h"
#include "struct.h"
#include "hash.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

extern ConfigFileEntryType ConfigFileEntry; /* defined in ircd.c */

/* Local function prototypes */
static int isnumber(char *);    /* return 0 if not, else return number */
static char *cluster(char *);

/*
 * Linked list of pending klines that need to be written to
 * the conf
 */
aPendingLine *PendingLines = NULL;

#ifdef SLAVE_SERVERS
extern aConfItem *find_special_conf(char *,int); /* defined in s_conf.c */
#endif

/*
 * LockFile routines
 */
static aPendingLine *AddPending(void);
static void DelPending(aPendingLine *);
static int LockedFile(const char *);
static void WritePendingLines(const char *);
static void WriteKline(const char *, struct Client *, struct Client *,
                       const char *, const char *, const char *, 
                       const char *, const char *);
static void WriteDline(const char *, struct Client *,
                       const char *, const char *,
		       const char *, const char *);

/*
AddPending()
 Add a pending K/D line to our linked list
*/

static aPendingLine *
AddPending(void)

{
  aPendingLine *temp;

  temp = (aPendingLine *) MyMalloc(sizeof(aPendingLine));

  /*
   * insert the new entry into our list
   */
  temp->next = PendingLines;
  PendingLines = temp;

  return (temp);
} /* AddPending() */

/*
DelPending()
 Delete pending line entry - assume calling function handles
linked list manipulation (setting next field etc)
*/

static void
DelPending(aPendingLine *pendptr)

{
  if (!pendptr)
    return;

  MyFree(pendptr->user);
  MyFree(pendptr->host);
  MyFree(pendptr->reason);
  MyFree(pendptr->oper_reason);
  MyFree(pendptr->when);
  MyFree(pendptr);
} /* DelPending() */

/*
LockedFile()
 Determine if 'filename' is currently locked. If it is locked,
there should be a filename.lock file which contains the current
pid of the editing process. Make sure the pid is valid before
giving up.

Return: 1 if locked
        0 if not
*/

static int
LockedFile(const char *filename)

{
  char lockpath[PATH_MAX + 1];
  char buffer[1024];
  FBFILE *fileptr;
  int killret;

  if (!filename)
    return (0);

  ircsprintf(lockpath, "%s.lock", filename);

  if ((fileptr = fbopen(lockpath, "r")) == NULL)
  {
    /*
     * lockfile does not exist
     */
    return (0);
  }

  if (fbgets(buffer, sizeof(buffer) - 1, fileptr))
  {
    /*
     * If it is a valid lockfile, 'buffer' should now
     * contain the pid number of the editing process.
     * Send the pid a SIGCHLD to see if it is a valid
     * pid - it could be a remnant left over from a
     * crashed editor or system reboot etc.
     */
    killret = kill(atoi(buffer), SIGCHLD);
    if (killret == 0)
    {
      fbclose(fileptr);
      return (1);
    }

    /*
     * killret must be -1, which indicates an error (most
     * likely ESRCH - No such process), so it is ok to
     * proceed writing klines.
     */
  }

  fbclose(fileptr);

  /*
   * Delete the outdated lock file
   */
  unlink(lockpath);

  return (0);
} /* LockedFile() */

static void
WritePendingLines(const char *filename)

{
  aPendingLine *ptmp;

  if (!filename)
    return;

  while (PendingLines)
  {
    if (PendingLines->type == KLINE_TYPE)
    {
      WriteKline(filename,
		 PendingLines->sptr,
		 PendingLines->rcptr,
		 PendingLines->user,
		 PendingLines->host,
		 PendingLines->reason,
		 PendingLines->oper_reason,
		 PendingLines->when);
    }
    else
    {
      WriteDline(filename,
		 PendingLines->sptr,
		 PendingLines->host,
		 PendingLines->reason,
		 PendingLines->oper_reason,
		 PendingLines->when);
    }

    /*
     * Delete the K/D line from the list after we write
     * it out to the conf
     */
    ptmp = PendingLines->next;
    DelPending(PendingLines);
    PendingLines = ptmp;
  } /* while (PendingLines) */
} /* WritePendingLines() */

/*
 * WriteKline()
 *  Write out a kline to the kline configuration file
 */

static void
WriteKline(const char *filename, struct Client *sptr, struct Client *rcptr,
           const char *user, const char *host,
	   const char *reason, const char *oper_reason,
           const char *when)

{
  char buffer[1024];
  int out;

  if (filename == NULL)
    return;

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT, 0644)) == (-1))
  {
    sendto_realops("Error opening %s: %s",
      filename,
      strerror(errno));
    return;
  }

  fchmod(out, 0660);

#ifdef SLAVE_SERVERS
  if (IsServer(sptr))
  {
    if (rcptr != NULL)
      ircsprintf(buffer,
		 "#%s!%s@%s from %s K'd: %s@%s:%s\n",
		 rcptr->name,
		 rcptr->username,
		 rcptr->host,
		 sptr->name,
		 user,
		 host,
		 reason);
  }
  else
#endif /* SLAVE_SERVERS */
  {
    if (oper_reason != NULL)
      {
	ircsprintf(buffer,
		   "#%s!%s@%s K'd: %s@%s:%s|%s\n",
		   sptr->name,
		   sptr->username,
		   sptr->host,
		   user,
		   host,
		   reason,
		   oper_reason);
      }
    else
      {
	ircsprintf(buffer,
		   "#%s!%s@%s K'd: %s@%s:%s\n",
		   sptr->name,
		   sptr->username,
		   sptr->host,
		   user,
		   host,
		   reason);
      }

  }

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  if (oper_reason != NULL)
    {
      ircsprintf(buffer, "K:%s:%s (%s) |%s:%s\n",
		 host,
		 reason,
		 when,
		 oper_reason,
		 user);
    }
  else
    {
      ircsprintf(buffer, "K:%s:%s (%s):%s\n",
		 host,
		 reason,
		 when,
		 user);
    }

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  (void) close(out);
} /* WriteKline() */

/*
WriteDline()
 Write out a dline to the kline configuration file
*/

static void
WriteDline(const char *filename, struct Client *sptr,
           const char *host, const char *reason, const char *oper_reason,
	   const char *when)

{
  char buffer[1024];
  int out;

  if (!filename)
    return;

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT, 0644)) == (-1))
  {
    sendto_realops("Error opening %s: %s",
      filename,
      strerror(errno));
    return;
  }

  fchmod(out, 0660);

  if(oper_reason != NULL)
    ircsprintf(buffer,
	       "#%s!%s@%s D'd: %s:%s|%s (%s)\n",
	       sptr->name,
	       sptr->username,
	       sptr->host,
	       host,
	       reason,
	       oper_reason,
	       when);
  else
    ircsprintf(buffer,
	       "#%s!%s@%s D'd: %s:%s (%s)\n",
	       sptr->name,
	       sptr->username,
	       sptr->host,
	       host,
	       reason,
	       when);


  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  ircsprintf(buffer, "D:%s:%s (%s)\n",
    host,
    reason,
    when);

  if (safe_write(sptr, filename, out, buffer) == (-1))
    return;

  (void) close(out);
} /* WriteDline() */

/*
 * m_kline()
 *
 * re-worked a tad ... -Dianora
 *
 * -Dianora
 */

int
m_kline(struct Client *cptr,
	struct Client *sptr,
	int parc,
	char *parv[])
{
  char buffer[512];
  char *p;
  char cidr_form_host[HOSTLEN + 1];
  char *user, *host;
  char *reason = NULL;
  char *oper_reason = NULL;
  const char* current_date;
  int  ip_kline = NO;
  struct Client *acptr;
  char tempuser[USERLEN + 2];
  char temphost[HOSTLEN + 1];
  struct ConfItem *aconf;
  int temporary_kline_time=0;   /* -Dianora */
  time_t temporary_kline_time_seconds=0;
  char *argv;
  unsigned long ip;
  unsigned long ip_mask;
  const char *kconf; /* kline conf file */
  char tmpch;
  int nonwild;

#ifdef SLAVE_SERVERS
  char *slave_oper;
  struct Client *rcptr=NULL;

  if(IsServer(sptr))
    {
      if(parc < 2)      /* pick up actual oper who placed kline */
        return 0;

      slave_oper = parv[1];     /* make it look like normal local kline */

      parc--;
      parv++;

      if ( parc < 2 )
        return 0;

      if ((rcptr = hash_find_client(slave_oper,NULL)))
        {
          if(!IsPerson(rcptr))
            return 0;
        }
      else
        return 0;

      if(!find_special_conf(sptr->name,CONF_ULINE))
        {
          sendto_realops("Received unauthorized K-line from %s",sptr->name);
          return 0;
        }
      else
        {
          sendto_realops("Received K-line from %s", sptr->name);
        }

#ifdef HUB
      sendto_slaves(sptr,"KLINE",slave_oper,parc,parv);
#endif

    }
  else
#endif
    {
      if (!MyClient(sptr) || !IsEmpowered(sptr))
        {
          sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
          return 0;
        }

      if(!IsSetOperK(sptr))
        {
          sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
          return 0;
        }

      if ( parc < 2 )
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                     me.name, parv[0], "KLINE");
          return 0;
        }

#ifdef SLAVE_SERVERS
      sendto_slaves(NULL,"KLINE",sptr->name,parc,parv);
#endif
    }

  argv = parv[1];

  if( (temporary_kline_time = isnumber(argv)) )
    {
      if(parc < 3)
        {
#ifdef SLAVE_SERVERS
          if(!IsServer(sptr))
#endif    
             sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                        me.name, parv[0], "KLINE");
          return 0;
        }
      if(temporary_kline_time > (24*60))
        temporary_kline_time = (24*60); /* Max it at 24 hours */
      temporary_kline_time_seconds = (time_t)temporary_kline_time * (time_t)60;
        /* turn it into minutes */
      argv = parv[2];
      parc--;
    }

  if ( (host = strchr(argv, '@')) || *argv == '*' )
    {
      /* Explicit user@host mask given */

      if(host)                  /* Found user@host */
        {
          user = argv;  /* here is user part */
          *(host++) = '\0';     /* and now here is host */
        }
      else
        {
          user = "*";           /* no @ found, assume its *@somehost */
          host = argv;
        }

      if (!*host)               /* duh. no host found, assume its '*' host */
        host = "*";
      strncpy_irc(tempuser, user, USERLEN + 1); /* allow for '*' in front */
      tempuser[USERLEN + 1] = '\0';
      strncpy_irc(temphost, host, HOSTLEN);
      temphost[HOSTLEN] = '\0';
      user = tempuser;
      host = temphost;
    }
  else
    {
      /* Try to find user@host mask from nick */

      if (!(acptr = find_chasing(sptr, argv, NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
#ifdef SLAVE_SERVERS
          if(!IsServer(sptr))
#endif
            sendto_one(sptr,
              ":%s NOTICE %s :Can't KLINE a server, use @'s where appropriate",
                       me.name, parv[0]);
          return 0;
        }

      if (IsAdmin(acptr))
        {
         /* Disallow use of KLINE against IRC Administrators -malign */
         sendto_one(sptr,"Unable to KLINE IRC Administrators.");
         return 0;
        }

      if(IsElined(acptr))
        {
#ifdef SLAVE_SERVERS
          if(!IsServer(sptr))
#endif
            sendto_one(sptr,
                       ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                       acptr->name);
          return 0;
        }

      /* turn the "user" bit into "*user", blow away '~'
         if found in original user name (non-idented) */

      tempuser[0] = '*';
      if (*acptr->username == '~')
        strcpy(tempuser+1, (char *)acptr->username+1);
      else
        strcpy(tempuser+1, acptr->username);
      user = tempuser;
      host = cluster(acptr->host);
    }

  if(temporary_kline_time)
    argv = parv[3];
  else
    argv = parv[2];

  if (parc > 2) 
    {
      if(strchr(argv, ':'))
        {
#ifdef SLAVE_SERVERS
          if(!IsServer(sptr))
#endif
            sendto_one(sptr,
                       ":%s NOTICE %s :Invalid character ':' in comment",
                       me.name, parv[0]);
          return 0;
        }

      if(strchr(argv, '#'))
        {
#ifdef SLAVE_SERVERS
          if(!IsServer(sptr))
#endif
            sendto_one(sptr,
                       ":%s NOTICE %s :Invalid character '#' in comment",
                       me.name, parv[0]);
          return 0;
        }

      if(*argv)
        reason = argv;
    }

  /*
   * Check for # in user@host
   */

  if(strchr(host, '#'))
    {
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in hostname",
                   me.name, parv[0]);
      return 0;
    }
  if(strchr(user, '#'))
    { 
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif  
        sendto_one(sptr, ":%s NOTICE %s :Invalid character '#' in username",
                   me.name, parv[0]);
      return 0;
    }   

  /*
   * Now we must check the user and host to make sure there
   * are at least NONWILDCHARS non-wildcard characters in
   * them, otherwise assume they are attempting to kline
   * *@* or some variant of that. This code will also catch
   * people attempting to kline *@*.tld, as long as NONWILDCHARS
   * is greater than 3. In that case, there are only 3 non-wild
   * characters (tld), so if NONWILDCHARS is 4, the kline will
   * be disallowed.
   * -wnder
   */

  nonwild = 0;
  p = user;
  while ((tmpch = *p++))
  {
    if (!IsKWildChar(tmpch))
    {
      /*
       * If we find enough non-wild characters, we can
       * break - no point in searching further.
       */
      if (++nonwild >= NONWILDCHARS)
        break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * The user portion did not contain enough non-wild
     * characters, try the host.
     */
    p = host;
    while ((tmpch = *p++))
    {
      if (!IsKWildChar(tmpch))
        if (++nonwild >= NONWILDCHARS)
          break;
    }
  }

  if (nonwild < NONWILDCHARS)
  {
    /*
     * Not enough non-wild characters were found, assume
     * they are trying to kline *@*.
     */
#ifdef SLAVE_SERVERS
    if (!IsServer(sptr))
#endif
      sendto_one(sptr,
        ":%s NOTICE %s :Please include at least %d non-wildcard characters with the user@host",
        me.name,
        parv[0],
        NONWILDCHARS);

    return 0;
  }

  /* 
  ** At this point, I know the user and the host to place the k-line on
  ** I also know whether its supposed to be a temporary kline or not
  ** I also know the reason field is clean
  ** Now what I want to do, is find out if its a kline of the form
  **
  ** /quote kline *@192.168.0.* i.e. it should be turned into a d-line instead
  **
  */


  if((ip_kline = is_address(host, &ip, &ip_mask)))
     {
       /*
        * XXX - ack
        */
       strncpy_irc(cidr_form_host, host, 32);
       cidr_form_host[32] = '\0';
       p = strchr(cidr_form_host,'*');
       if (p)
         {
           *p++ = '0';
           *p++ = '/';
           *p++ = '2';
           *p++ = '4';
           *p++ = '\0';
         }
       host = cidr_form_host;
    }
  else
    {
      ip = 0L;
    }

  if( (aconf = is_klined(host,user,(unsigned long)ip)) )
     {
       char *nrkreason;

       if( aconf->status & CONF_KILL )
         {
           nrkreason = aconf->passwd ? aconf->passwd : "<No Reason>";
#ifdef SLAVE_SERVERS
           if(!IsServer(sptr))
#endif
             sendto_one(sptr,
                        ":%s NOTICE %s :[%s@%s] already K-lined by [%s@%s] - %s",
                        me.name,
                        parv[0],
                        user,host,
                        aconf->user,aconf->host,nrkreason);
           return 0;
         }
     }

  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host, host);
  DupString(aconf->user, user);
  aconf->port = 0;

  if (reason == NULL)
    reason = "No reason";
  else
    {
      if ((p = strchr(reason, '|')) != NULL)
	{
	  *p = '\0';
	  p++;
	  oper_reason = p;
	}
    }

  if(temporary_kline_time)
    {
      ircsprintf(buffer,
		 "Temporary K-line %d min. - %s (%s)",
		 temporary_kline_time,
		 reason,
		 current_date);
      DupString(aconf->passwd, buffer );

      aconf->hold = CurrentTime + temporary_kline_time_seconds;
      if(ip_kline)
        {
          aconf->ip = ip;
          aconf->ip_mask = ip_mask;
        }
      add_temp_kline(aconf);
      rehashed = YES;
      dline_in_progress = NO;
      nextping = CurrentTime;
      sendto_realops("%s added temporary %d min. K-Line for [%s@%s] [%s]",
		     parv[0],
		     temporary_kline_time,
		     user,
		     host,
		     reason);
      return 0;
    }
  else
    {
      ircsprintf(buffer, "%s (%s)", reason, current_date);
      DupString(aconf->passwd, buffer);
    }
  ClassPtr(aconf) = find_class(0);

  if(ip_kline)
    {
      aconf->ip = ip;
      aconf->ip_mask = ip_mask;
      add_ip_Kline(aconf);
    }
  else
    add_mtrie_conf_entry(aconf,CONF_KILL);

  sendto_realops("%s added K-Line for [%s@%s] [%s]",
		 sptr->name,
		 user,
		 host,
		 reason);


  Log("%s issued KLINE for [%s@%s] [%s|%s]",
      sptr->name, user, host, reason, oper_reason ? oper_reason : "");

  kconf = get_conf_name(KLINE_TYPE);

  /*
   * Check if the conf file is locked - if so, add the kline
   * to our pending kline list, to be written later, if not,
   * allow this kline to be written, and write out all other
   * pending klines as well
   */
  if (LockedFile(kconf))
  {
    aPendingLine *pptr;

    pptr = AddPending();

    /*
     * Now fill in the fields
     */
    pptr->type = KLINE_TYPE;
    pptr->sptr = sptr;
    DupString(pptr->user, user);
    DupString(pptr->host, host);
    DupString(pptr->reason, reason);
    DupString(pptr->when, current_date);

#ifdef SLAVE_SERVERS
    pptr->rcptr = rcptr;
#else
    pptr->rcptr = NULL;
#endif

    sendto_one(sptr,
      ":%s NOTICE %s :Added K-Line [%s@%s] (config file write delayed)",
      me.name,
      sptr->name,
      user,
      host);

    return 0;
  }
  else if (PendingLines)
    WritePendingLines(kconf);

  sendto_one(sptr,
    ":%s NOTICE %s :Added K-Line [%s@%s] to %s",
    me.name,
    sptr->name,
    user,
    host,
    kconf ? kconf : "configuration file");

  /*
   * Write kline to configuration file
   */
#ifdef SLAVE_SERVERS
  WriteKline(kconf,
	     sptr,
	     rcptr,
	     user,
	     host,
	     reason,
	     oper_reason,
	     current_date);
#else
  WriteKline(kconf,
	     sptr,
	     NULL,
	     user,
	     host,
	     reason,
	     oper_reason,
	     current_date);
#endif

  rehashed = YES;
  dline_in_progress = NO;
  nextping = CurrentTime;
  return 0;
} /* m_kline() */

/*
 * isnumber()
 * 
 * inputs       - pointer to ascii string in
 * output       - 0 if not an integer number, else the number
 * side effects - none
*/

static int isnumber(char *p)
{
  int result = 0;

  while(*p)
    {
      if(IsDigit(*p))
        {
          result *= 10;
          result += ((*p) & 0xF);
          p++;
        }
      else
        return(0);
    }
  /* in the degenerate case where oper does a /quote kline 0 user@host :reason 
     i.e. they specifically use 0, I am going to return 1 instead
     as a return value of non-zero is used to flag it as a temporary kline
  */

  if(result == 0)
    result = 1;
  return(result);
}

/*
 * cluster()
 *
 * input        - pointer to a hostname
 * output       - pointer to a static of the hostname masked
 *                for use in a kline.
 * side effects - NONE
 *
 * reworked a tad -Dianora
 */

static char *cluster(char *hostname)
{
  static char result[HOSTLEN + 1];      /* result to return */
  char        temphost[HOSTLEN + 1];    /* work place */
  char        *ipp;             /* used to find if host is ip # only */
  char        *host_mask;       /* used to find host mask portion to '*' */
  char        *zap_point = NULL; /* used to zap last nnn portion of an ip # */
  char        *tld;             /* Top Level Domain */
  int         is_ip_number;     /* flag if its an ip # */             
  int         number_of_dots;   /* count number of dots for both ip# and
                                   domain klines */
  if (!hostname)
    return (char *) NULL;       /* EEK! */

  /* If a '@' is found in the hostname, this is bogus
   * and must have been introduced by server that doesn't
   * check for bogus domains (dns spoof) very well. *sigh* just return it...
   * I could also legitimately return NULL as above.
   *
   * -Dianora
   */

  if(strchr(hostname,'@'))      
    {
      strncpy_irc(result, hostname, HOSTLEN);      
      result[HOSTLEN] = '\0';
      return(result);
    }

  strncpy_irc(temphost, hostname, HOSTLEN);
  temphost[HOSTLEN] = '\0';

  is_ip_number = YES;   /* assume its an IP# */
  ipp = temphost;
  number_of_dots = 0;

  while (*ipp)
    {
      if( *ipp == '.' )
        {
          number_of_dots++;

          if(number_of_dots == 3)
            zap_point = ipp;
          ipp++;
        }
      else if(!IsDigit(*ipp))
        {
          is_ip_number = NO;
          break;
        }
      ipp++;
    }

  if(is_ip_number && (number_of_dots == 3))
    {
      zap_point++;
      *zap_point++ = '*';               /* turn 111.222.333.444 into */
      *zap_point = '\0';                /*      111.222.333.*        */
      strncpy_irc(result, temphost, HOSTLEN);
      result[HOSTLEN] = '\0';
      return(result);
    }
  else
    {
      tld = strrchr(temphost, '.');
      if(tld)
        {
          number_of_dots = 2;
          if(tld[3])                     /* its at least a 3 letter tld
                                            i.e. ".com" tld[3] = 'm' not 
                                            '\0' */
                                         /* 4 letter tld's are coming */
            number_of_dots = 1;

          if(tld != temphost)           /* in these days of dns spoofers ...*/
            host_mask = tld - 1;        /* Look for host portion to '*' */
          else
            host_mask = tld;            /* degenerate case hostname is
                                           '.com' etc. */

          while (host_mask != temphost)
            {
              if(*host_mask == '.')
                number_of_dots--;
              if(number_of_dots == 0)
                {
                  result[0] = '*';
                  strncpy_irc(result + 1, host_mask, HOSTLEN - 1);
                  result[HOSTLEN] = '\0';
                  return(result);
                }
              host_mask--;
            }
          result[0] = '*';                      /* foo.com => *foo.com */
          strncpy_irc(result + 1, temphost, HOSTLEN - 1);
          result[HOSTLEN] = '\0';
        }
      else      /* no tld found oops. just return it as is */
        {
          strncpy_irc(result, temphost, HOSTLEN);
          result[HOSTLEN] = '\0';
          return(result);
        }
    }

  return (result);
}

/*
 * re-worked a tad
 * added Rodders dated KLINE code
 * -Dianora
 *
 */

int
m_dline(struct Client *cptr, struct Client *sptr, int parc, char *parv[])

{
  char *host, *reason;
  char *oper_reason = NULL;
  char *p;
  struct Client *acptr;
  char cidr_form_host[HOSTLEN + 1];
  unsigned long ip_host;
  unsigned long ip_mask;
  aConfItem *aconf;
  char buffer[1024];
  const char* current_date;
  const char *dconf;

  if (!MyClient(sptr) || !IsEmpowered(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if(!IsSetOperK(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "DLINE");
      return 0;
    }

  host = parv[1];
  strncpy_irc(cidr_form_host, host, 32);
  cidr_form_host[32] = '\0';

  if((p = strchr(cidr_form_host,'*')))
    {
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      host = cidr_form_host;
    }

  if(!is_address(host,&ip_host,&ip_mask))
    {
      if (!(acptr = find_chasing(sptr, parv[1], NULL)))
        return 0;

      if(!acptr->user)
        return 0;

      if (IsServer(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Can't DLINE a server silly",
                     me.name, parv[0]);
          return 0;
        }
              
      if(!MyConnect(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE :%s :Can't DLINE nick on another server",
                     me.name, parv[0]);
          return 0;
        }

      if(IsElined(acptr))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
                     acptr->name);
          return 0;
        }

      /*
       * XXX - this is always a fixed length output, we can get away
       * with strcpy here
       *
       * strncpy_irc(cidr_form_host, inetntoa((char *)&acptr->ip), 32);
       * cidr_form_host[32] = '\0';
       */
       strcpy(cidr_form_host, inetntoa((char*) &acptr->ip));
      
      p = strchr(cidr_form_host,'.');
      if(!p)
        return 0;
      /* 192. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return 0;
      /* 192.168. <- p */

      p++;
      p = strchr(p,'.');
      if(!p)
        return 0;
      /* 192.168.0. <- p */

      p++;
      *p++ = '0';
      *p++ = '/';
      *p++ = '2';
      *p++ = '4';
      *p++ = '\0';
      host = cidr_form_host;

      ip_mask = 0xFFFFFF00L;
      ip_host = ntohl(acptr->ip.s_addr);
    }


  if (parc > 2) /* host :reason */
    {
      if(strchr(parv[2], ':'))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Invalid character ':' in comment",
                     me.name, parv[0]);
          return 0;
        }

      if(strchr(parv[2], '#'))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Invalid character '#' in comment",
                     me.name, parv[0]);
          return 0;
        }

      if(*parv[2])
        reason = parv[2];
      else
        reason = "No reason";
    }
  else
    reason = "No reason";

  if ((p = strchr(reason, '|')) != NULL)
    {
      *p = '\0';
      p++;
      oper_reason = p;
    }

  if((ip_mask & 0xFFFFFF00) ^ 0xFFFFFF00)
    {
      if(ip_mask != 0xFFFFFFFF)
        {
          sendto_one(sptr, ":%s NOTICE %s :Can't use a mask less than 24 with dline",
                     me.name,
                     parv[0]);
          return 0;
        }
    }

  if( (aconf = match_Dline(ip_host)) )
     {
       char *nrkreason;
       nrkreason = aconf->passwd ? aconf->passwd : "<No Reason>";
       if(IsConfElined(aconf))
         sendto_one(sptr, ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
                    me.name,
                    parv[0],
                    host,
                    aconf->host,nrkreason);
         else
           sendto_one(sptr, ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
                      me.name,
                      parv[0],
                      host,
                      aconf->host,nrkreason);
      return 0;
       
     }

  current_date = smalldate((time_t) 0);

  ircsprintf(buffer, "%s (%s)",reason,current_date);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,host);
  DupString(aconf->passwd,buffer);

  aconf->ip = ip_host;
  aconf->ip_mask = ip_mask;

  add_Dline(aconf);

  sendto_realops("%s added D-Line for [%s] [%s]",
		 sptr->name,
		 host,
		 reason);

  Log("%s added D-Line for [%s] [%s|%s]", 
      sptr->name, host, reason,
      oper_reason ? oper_reason : "" );

  dconf = get_conf_name(DLINE_TYPE);

  /*
   * Check if the conf file is locked - if so, add the dline
   * to our pending dline list, to be written later, if not,
   * allow this dline to be written, and write out all other
   * pending lines as well
   */
  if (LockedFile(dconf))
    {
      aPendingLine *pptr;
      
      pptr = AddPending();
      
      /*
       * Now fill in the fields
       */
      pptr->type = DLINE_TYPE;
      pptr->sptr = sptr;
      pptr->rcptr = NULL;
      pptr->user = NULL;
      pptr->host = strdup(host);
      pptr->reason = strdup(reason);
      if (oper_reason != NULL)
	pptr->oper_reason = strdup(oper_reason);
      pptr->when = strdup(current_date);

      sendto_one(sptr,
		 ":%s NOTICE %s :Added D-Line [%s] (config file write delayed)",
		 me.name,
		 sptr->name,
		 host);

      return 0;
    }
  else if (PendingLines)
    WritePendingLines(dconf);
  
  sendto_one(sptr,
	     ":%s NOTICE %s :Added D-Line [%s] to %s",
	     me.name,
	     sptr->name,
	     host,
	     dconf ? dconf : "configuration file");
  
  /*
   * Write dline to configuration file
   */
  WriteDline(dconf,
	     sptr,
	     host,
	     reason,
	     oper_reason,
	     current_date);

  /*
  ** I moved the following 2 lines up here
  ** because we still want the server to
  ** hunt for 'targetted' clients even if
  ** there are problems adding the D-line
  ** to the appropriate file. -ThemBones
  */
  rehashed = YES;
  dline_in_progress = YES;
  nextping = CurrentTime;
  return 0;
} /* m_dline() */
