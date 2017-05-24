/************************************************************************
 *   IRC - Internet Relay Chat, src/parse.c
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
 *   $Id: parse.c,v 1.1.1.1 2006/03/08 23:28:11 malign Exp $
 */
#include "parse.h"
#include "channel.h"
#include "client.h"
#include "common.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_stats.h"
#include "send.h"
#include "struct.h"

#ifdef DEBUGMODE
#include "s_debug.h"
#endif

#define MSGTAB
#include "msg.h"
#undef MSGTAB

#include <assert.h>
#include <string.h>
#include <stdlib.h>

/*
 * NOTE: parse() should not be called recursively by other functions!
 */
static  char    *para[MAXPARA+1];
static  int     cancel_clients (aClient *, aClient *, char *);
static  void    remove_unknown (aClient *, char *, char *);

static  char    sender[HOSTLEN+1];
static  int     cancel_clients (aClient *, aClient *, char *);
static  void    remove_unknown (aClient *, char *, char *);

static int do_numeric (char [], struct Client *,
                         struct Client *, int, char **);

static struct Message *do_msg_tree(MESSAGE_TREE *, char *, struct Message *);
static struct Message *tree_parse(char *);

static char buffer[1024];  /* ZZZ must this be so big? must it be here? */

/*
 * parse a buffer.
 *
 * NOTE: parse() should not be called recusively by any other functions!
 */
int parse(aClient *cptr, char *pbuffer, char *bufend)
{
  aClient *from = cptr;
  char  *ch;
  char  *s;
  int   i;
  char* numeric = 0;
  int   paramcount;
  struct Message *mptr;

  Debug((DEBUG_DEBUG, "Parsing %s: %s",
         get_client_name(cptr, TRUE), pbuffer));

  if (IsDead(cptr))
    return -1;

  s = sender;
  *s = '\0';

  for (ch = pbuffer; *ch == ' '; ch++)   /* skip spaces */
    /* null statement */ ;

  para[0] = from->name;
  if (*ch == ':')
    {
      ch++;

      /*
      ** Copy the prefix to 'sender' assuming it terminates
      ** with SPACE (or NULL, which is an error, though).
      */
      for (i = 0; *ch && *ch != ' '; i++ )
	{
	  if (i < (sizeof(sender)-1))
	    *s++ = *ch; /* leave room for NULL */
	  ch++;
	}
      *s = '\0';
      i = 0;

      /*
      ** Actually, only messages coming from servers can have
      ** the prefix--prefix silently ignored, if coming from
      ** a user client...
      **
      ** ...sigh, the current release "v2.2PL1" generates also
      ** null prefixes, at least to NOTIFY messages (e.g. it
      ** puts "sptr->nickname" as prefix from server structures
      ** where it's null--the following will handle this case
      ** as "no prefix" at all --msa  (": NOTICE nick ...")
      */
      if (*sender && IsServer(cptr))
        {
          from = find_client(sender, (aClient *) NULL);
          if (!from || !match(from->name, sender))
            from = find_server(sender);

          para[0] = sender;
          
          /* Hmm! If the client corresponding to the
           * prefix is not found--what is the correct
           * action??? Now, I will ignore the message
           * (old IRC just let it through as if the
           * prefix just wasn't there...) --msa
           */
          if (!from)
            {
              Debug((DEBUG_ERROR, "Unknown prefix (%s)(%s) from (%s)",
                     sender, pbuffer, cptr->name));
              ServerStats->is_unpf++;

              remove_unknown(cptr, sender, pbuffer);

              return -1;
            }
          if (from->from != cptr)
            {
              ServerStats->is_wrdi++;
              Debug((DEBUG_ERROR, "Message (%s) coming from (%s)",
                     pbuffer, cptr->name));

              return cancel_clients(cptr, from, pbuffer);
            }
        }
      while (*ch == ' ')
        ch++;
    }

  if (*ch == '\0')
    {
      ServerStats->is_empt++;
      Debug((DEBUG_NOTICE, "Empty message from host %s:%s",
             cptr->name, from->name));
      return(-1);
    }

  /*
  ** Extract the command code from the packet.  Point s to the end
  ** of the command code and calculate the length using pointer
  ** arithmetic.  Note: only need length for numerics and *all*
  ** numerics must have parameters and thus a space after the command
  ** code. -avalon
  *
  * ummm???? - Dianora
  */

  if( *(ch + 3) == ' ' && /* ok, lets see if its a possible numeric.. */
      IsDigit(*ch) && IsDigit(*(ch + 1)) && IsDigit(*(ch + 2)) )
    {
      mptr = (struct Message *)NULL;
      numeric = ch;
      paramcount = MAXPARA;
      ServerStats->is_num++;
      s = ch + 3;       /* I know this is ' ' from above if */
      *s++ = '\0';      /* blow away the ' ', and point s to next part */
    }
  else
    {
      s = strchr(ch, ' ');      /* moved from above,now need it here */
      if (s)
        *s++ = '\0';

      mptr = tree_parse(ch);

      if (!mptr || !mptr->cmd)
        {
          /*
          ** Note: Give error message *only* to recognized
          ** persons. It's a nightmare situation to have
          ** two programs sending "Unknown command"'s or
          ** equivalent to each other at full blast....
          ** If it has got to person state, it at least
          ** seems to be well behaving. Perhaps this message
          ** should never be generated, though...  --msa
          ** Hm, when is the buffer empty -- if a command
          ** code has been found ?? -Armin
          */
          if (pbuffer[0] != '\0')
            {
              if (IsPerson(from))
                sendto_one(from,
                           ":%s %d %s %s :Unknown command",
                           me.name, ERR_UNKNOWNCOMMAND,
                           from->name, ch);
              Debug((DEBUG_ERROR,"Unknown (%s) from %s",
                     ch, get_client_name(cptr, TRUE)));
            }
          ServerStats->is_unco++;
          return(-1);
        }

      paramcount = mptr->parameters;
      i = bufend - ((s) ? s : ch);
      mptr->bytes += i;

      /* Allow only 1 msg per 2 seconds
       * (on average) to prevent dumping.
       * to keep the response rate up,
       * bursts of up to 5 msgs are allowed
       * -SRB
       * Opers can send 1 msg per second, burst of ~20
       * -Taner
       */
      if ((mptr->flags & 1) && !(IsServer(cptr)))
        {
#ifdef NO_OPER_FLOOD
#ifndef TRUE_NO_OPER_FLOOD
/* note: both have to be defined for the real no-flood */
          if (IsEmpowered(cptr))
            /* "randomly" (weighted) increase the since */
            cptr->since += (cptr->receiveM % 5) ? 1 : 0;
          else
#else
          if (!IsEmpowered(cptr))
#endif
#endif
            cptr->since += (2 + i / 120);
        }
    }
  /*
  ** Must the following loop really be so devious? On
  ** surface it splits the message to parameters from
  ** blank spaces. But, if paramcount has been reached,
  ** the rest of the message goes into this last parameter
  ** (about same effect as ":" has...) --msa
  */

  /* Note initially true: s==NULL || *(s-1) == '\0' !! */

  /* ZZZ hmmmmmmmm whats this then? */
#if 0
  if (me.user)
    para[0] = sender;
#endif

  i = 1;

  if (s)
    {
      if (paramcount > MAXPARA)
        paramcount = MAXPARA;

      for (;;)
        {
	  while(*s == ' ')	/* tabs are not considered space */
	    *s++ = '\0';

          if(!*s)
            break;

          if (*s == ':')
            {
              /*
              ** The rest is single parameter--can
              ** include blanks also.
              */
              para[i++] = s + 1;
              break;
            }
	  else
	    {
	      para[i++] = s;
              if (i >= paramcount)
                {
                  break;
                }
              /* scan for end of string, either ' ' or '\0' */
              while (IsNonEOS(*s))
                s++;
	    }
        }
    }

  para[i] = NULL;
  if (mptr == (struct Message *)NULL)
    return (do_numeric(numeric, cptr, from, i, para));

  mptr->count++;

  /* patch to avoid server flooding from unregistered connects */
  /* check allow_unregistered_use flag I've set up instead of function
     comparing *yech* - Dianora */

  if (!IsRegistered(cptr) && !mptr->allow_unregistered_use )
    {
      /* if its from a possible server connection
       * ignore it.. more than likely its a header thats sneaked through
       */

      if(IsHandshake(cptr) || IsConnecting(cptr) || IsServer(cptr))
        return -1;

      sendto_one(from,
                 ":%s %d %s %s :Register first.",
                 me.name, ERR_NOTREGISTERED,
                 BadPtr(from->name) ? "*" : from->name, ch);
      return -1;
    }

  /* Again, instead of function address comparing, see if
   * this function resets idle time as given from mptr
   * if IDLE_FROM_MSG is undefined, the sense of the flag is reversed.
   * i.e. if the flag is 0, then reset idle time, otherwise don't reset it.
   *
   * - Dianora
   */

#ifdef IDLE_FROM_MSG
  if (IsRegisteredUser(cptr) && mptr->reset_idle)
    from->user->last = CurrentTime;
#else
  if (IsRegisteredUser(cptr) && !mptr->reset_idle)
    from->user->last = CurrentTime;
#endif
  
  /* don't allow other commands while a list is blocked. since we treat
     them specially with respect to sendq. */
  if ((IsDoingList(cptr)) && (*mptr->func != m_list))
      return -1;
  return (*mptr->func)(cptr, from, i, para);
}

/* for qsort'ing the msgtab in place -orabidoo */
static int mcmp(struct Message *m1, struct Message *m2)
{
  return strcmp(m1->cmd, m2->cmd);
}

/*
 * init_tree_parse()
 *
 * inputs               - pointer to msg_table defined in msg.h 
 * output       - NONE
 * side effects - MUST MUST be called at startup ONCE before
 *                any other keyword hash routine is used.
 *
 *      -Dianora, orabidoo
 */
/* Initialize the msgtab parsing tree -orabidoo
 */
void init_tree_parse(struct Message *mptr)
{
  int i;
  struct Message *mpt = mptr;

  for (i=0; mpt->cmd; mpt++)
    i++;
  qsort((void *)mptr, i, sizeof(struct Message), 
                (int (*)(const void *, const void *)) mcmp);
  msg_tree_root = (MESSAGE_TREE *)MyMalloc(sizeof(MESSAGE_TREE));
  mpt = do_msg_tree(msg_tree_root, "", mptr);

  /*
   * this happens if one of the msgtab entries included characters
   * other than capital letters  -orabidoo
   */
  if (mpt->cmd)
    {
      Log("bad msgtab entry: ``%s''\n", mpt->cmd);
      exit(1);
    }
}

/*  Recursively make a prefix tree out of the msgtab -orabidoo
 */
static struct Message *do_msg_tree(MESSAGE_TREE *mtree, char *prefix,
                                struct Message *mptr)
{
  char newpref[64];  /* must be longer than any command */
  int c, c2, lp;
  MESSAGE_TREE *mtree1;

  lp = strlen(prefix);
  if (!lp || !strncmp(mptr->cmd, prefix, lp))
    {
      if (!mptr[1].cmd || (lp && strncmp(mptr[1].cmd, prefix, lp)))
        {
          /* non ambiguous -> make a final case */
          mtree->final = mptr->cmd + lp;
          mtree->msg = mptr;
          for (c=0; c<=25; c++)
            mtree->pointers[c] = NULL;
          return mptr+1;
        }
      else
        {
          /* ambigous -> make new entries for each of the letters that match */
          if (!irccmp(mptr->cmd, prefix))
            {
              /* fucking OPERWALL blows me */
              mtree->final = (void *)1;
              mtree->msg = mptr;
              mptr++;
            }
          else
            mtree->final = NULL;

      for (c='A'; c<='Z'; c++)
        {
          if (mptr->cmd[lp] == c)
            {
              mtree1 = (MESSAGE_TREE *)MyMalloc(sizeof(MESSAGE_TREE));
              mtree1->final = NULL;
              mtree->pointers[c-'A'] = mtree1;
              strcpy(newpref, prefix);
              newpref[lp] = c;
              newpref[lp+1] = '\0';
              mptr = do_msg_tree(mtree1, newpref, mptr);
              if (!mptr->cmd || strncmp(mptr->cmd, prefix, lp))
                {
                  for (c2=c+1-'A'; c2<=25; c2++)
                    mtree->pointers[c2] = NULL;
                  return mptr;
                }
            } 
          else
            {
              mtree->pointers[c-'A'] = NULL;
            }
        }
      return mptr;
        }
    } 
  else
    {
      assert(0);
      exit(1);
    }
  return (0); 
}

/*
 * tree_parse()
 * 
 * inputs       - pointer to command in upper case
 * output       - NULL pointer if not found
 *                struct Message pointer to command entry if found
 * side effects - NONE
 *
 *      -Dianora, orabidoo
 */

static struct Message *tree_parse(char *cmd)
{
  char r;
  MESSAGE_TREE *mtree = msg_tree_root;

  while ((r = *cmd++))
    {
      r &= 0xdf;  /* some touppers have trouble w/ lowercase, says Dianora */
      if (r < 'A' || r > 'Z')
        return NULL;
      mtree = mtree->pointers[r - 'A'];
      if (!mtree)
        return NULL;
      if (mtree->final == (void *)1)
        {
          if (!*cmd)
            return mtree->msg;
        }
      else
        if (mtree->final && !irccmp(mtree->final, cmd))
          return mtree->msg;
    }
  return ((struct Message *)NULL);
}

static  int     cancel_clients(aClient *cptr,
                               aClient *sptr,
                               char *cmd)
{
  /*
   * kill all possible points that are causing confusion here,
   * I'm not sure I've got this all right...
   * - avalon
   */

  /*
  ** with TS, fake prefixes are a common thing, during the
  ** connect burst when there's a nick collision, and they
  ** must be ignored rather than killed because one of the
  ** two is surviving.. so we don't bother sending them to
  ** all ops everytime, as this could send 'private' stuff
  ** from lagged clients. we do send the ones that cause
  ** servers to be dropped though, as well as the ones from
  ** non-TS servers -orabidoo
  */
  /*
   * Incorrect prefix for a server from some connection.  If it is a
   * client trying to be annoying, just QUIT them, if it is a server
   * then the same deal.
   */
  if (IsServer(sptr) || IsMe(sptr))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops_flags(FLAGS_DEBUG, "Message for %s[%s] from %s",
      			 sptr->name, sptr->from->name,
			 get_client_name(cptr, MASK_IP));
#else			 
      sendto_realops_flags(FLAGS_DEBUG, "Message for %s[%s] from %s",
                         sptr->name, sptr->from->name,
                         get_client_name(cptr, TRUE));
#endif			 
      if (IsServer(cptr))
        {
          sendto_realops_flags(FLAGS_DEBUG,
                             "Not dropping server %s (%s) for Fake Direction",
                             cptr->name, sptr->name);
          return -1;
        }

      if (IsClient(cptr))
        sendto_realops_flags(FLAGS_DEBUG,
                           "Would have dropped client %s (%s@%s) [%s from %s]",
                           cptr->name, cptr->username, cptr->host,
                           cptr->user->server, cptr->from->name);
      return -1;

      /*
        return exit_client(cptr, cptr, &me, "Fake Direction");
        */
    }
  /*
   * Ok, someone is trying to impose as a client and things are
   * confused.  If we got the wrong prefix from a server, send out a
   * kill, else just exit the lame client.
   */
  if (IsServer(cptr))
   {
    /*
    ** If the fake prefix is coming from a TS server, discard it
    ** silently -orabidoo
    */
    if (DoesTS(cptr))
      {
        if (sptr->user)
#ifdef HIDE_SERVERS_IPS
	  sendto_realops_flags(FLAGS_DEBUG,
	      "Message for %s[%s@%s!%s] from %s (TS, ignored)",
	      sptr->name, sptr->username, sptr->host,
	      sptr->from->name, get_client_name(cptr, MASK_IP));
#else	      
          sendto_realops_flags(FLAGS_DEBUG,
              "Message for %s[%s@%s!%s] from %s (TS, ignored)",
                             sptr->name, sptr->username, sptr->host,
                           sptr->from->name, get_client_name(cptr, TRUE));
#endif			   
        return 0;
      }
    else
      {
        if (sptr->user)
          {
#ifdef HIDE_SERVERS_IPS
	    sendto_realops_flags(FLAGS_DEBUG,
	    		"Message for %s[%s@%s!%s] from %s",
			sptr->name, sptr->username, sptr->host,
			sptr->from->name, get_client_name(cptr, MASK_IP));
#else			
            sendto_realops_flags(FLAGS_DEBUG,
                             "Message for %s[%s@%s!%s] from %s",
                           sptr->name, sptr->username, sptr->host,
                           sptr->from->name, get_client_name(cptr, TRUE));
#endif			   
          }
        sendto_serv_butone(NULL,
                           ":%s KILL %s :%s (%s[%s] != %s, Fake Prefix)",
                           me.name, sptr->name, me.name,
                           sptr->name, sptr->from->name,
                           cptr->name);
        sptr->flags |= FLAGS_KILLED;
        return exit_client(cptr, sptr, &me, "Fake Prefix");
      }
   }
  return exit_client(cptr, cptr, &me, "Fake prefix");
}

static  void    remove_unknown(aClient *cptr,
                               char *psender,
                               char *pbuffer)
{
  if (!IsRegistered(cptr))
    return;

  if (IsClient(cptr))
    {
      sendto_realops_flags(FLAGS_DEBUG,
                 "Weirdness: Unknown client prefix (%s) from %s, Ignoring %s",
                         pbuffer,
                         get_client_name(cptr, FALSE), psender);
      return;
    }

  /*
   * Not from a server so don't need to worry about it.
   */
  if (!IsServer(cptr))
    return;
  /*
   * Do kill if it came from a server because it means there is a ghost
   * user on the other server which needs to be removed. -avalon
   * Tell opers about this. -Taner
   */
  if (!strchr(psender, '.'))
    sendto_one(cptr, ":%s KILL %s :%s (%s(?) <- %s)",
               me.name, psender, me.name, psender,
               cptr->name);
  else
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops_flags(FLAGS_DEBUG,
              "Unknown prefix (%s) from %s, Squitting %s",
	      pbuffer, get_client_name(cptr, MASK_IP), psender);
      sendto_one(cptr, ":%s SQUIT %s :(Unknown prefix (%s) from %s)",
      		 me.name, psender, pbuffer, get_client_name(cptr, MASK_IP));
#else		 
      sendto_realops_flags(FLAGS_DEBUG,
        "Unknown prefix (%s) from %s, Squitting %s", pbuffer, get_client_name(cptr, FALSE), psender);
      sendto_one(cptr, ":%s SQUIT %s :(Unknown prefix (%s) from %s)",
                 me.name, psender, pbuffer, get_client_name(cptr, FALSE));
#endif		 
    }
}



/*
** DoNumeric (replacement for the old do_numeric)
**
**      parc    number of arguments ('sender' counted as one!)
**      parv[0] pointer to 'sender' (may point to empty string) (not used)
**      parv[1]..parv[parc-1]
**              pointers to additional parameters, this is a NULL
**              terminated list (parv[parc] == NULL).
**
** *WARNING*
**      Numerics are mostly error reports. If there is something
**      wrong with the message, just *DROP* it! Don't even think of
**      sending back a neat error message -- big danger of creating
**      a ping pong error message...
*/
static int     do_numeric(
                   char numeric[],
                   aClient *cptr,
                   aClient *sptr,
                   int parc,
                   char *parv[])
{
  aClient *acptr;
  aChannel *chptr;
  char  *nick, *p;
  int   i;

  if (parc < 1 || !IsServer(sptr))
    return 0;

  /* Remap low number numerics. */
  if(numeric[0] == '0')
    numeric[0] = '1';

  /*
  ** Prepare the parameter portion of the message into 'buffer'.
  ** (Because the buffer is twice as large as the message buffer
  ** for the socket, no overflow can occur here... ...on current
  ** assumptions--bets are off, if these are changed --msa)
  ** Note: if buffer is non-empty, it will begin with SPACE.
  */
  buffer[0] = '\0';
  if (parc > 1)
    {
      for (i = 2; i < (parc - 1); i++)
        {
          (void)strcat(buffer, " ");
          (void)strcat(buffer, parv[i]);
        }
      (void)strcat(buffer, " :");
      (void)strcat(buffer, parv[parc-1]);
    }
  for (; (nick = strtoken(&p, parv[1], ",")); parv[1] = NULL)
    {
      if ((acptr = find_client(nick, (aClient *)NULL)))
        {
          /*
          ** Drop to bit bucket if for me...
          ** ...one might consider sendto_ops
          ** here... --msa
          ** And so it was done. -avalon
          ** And regretted. Dont do it that way. Make sure
          ** it goes only to non-servers. -avalon
          ** Check added to make sure servers don't try to loop
          ** with numerics which can happen with nick collisions.
          ** - Avalon
          */
          if (!IsMe(acptr) && IsPerson(acptr))
            sendto_prefix_one(acptr, sptr,":%s %s %s%s",
                              parv[0], numeric, nick, buffer);
          else if (IsServer(acptr) && acptr->from != cptr)
            sendto_prefix_one(acptr, sptr,":%s %s %s%s",
                              parv[0], numeric, nick, buffer);
        }
      else if ((acptr = find_server(nick)))
        {
          if (!IsMe(acptr) && acptr->from != cptr)
            sendto_prefix_one(acptr, sptr,":%s %s %s%s",
                              parv[0], numeric, nick, buffer);
        }
      else if ((chptr = hash_find_channel(nick, (aChannel *)NULL)))
        sendto_channel_butone(cptr,sptr,chptr,":%s %s %s%s",
                              parv[0],
                              numeric, chptr->chname, buffer);
    }
  return 0;
}
