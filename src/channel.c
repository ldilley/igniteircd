/************************************************************************
 *   IRC - Internet Relay Chat, src/channel.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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
 * a number of behaviours in set_mode() have been rewritten
 * These flags can be set in a define if you wish.
 *
 *
 * $Id: channel.c,v 1.4 2007/02/14 10:00:42 malign Exp $
 */
#include "channel.h"
#include "m_commands.h"
#include "client.h"
#include "common.h"
#include "flud.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"       /* captab */
#include "s_user.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

int total_hackops = 0;
int total_ignoreops = 0;

#ifdef NEED_SPLITCODE
static void check_still_split();
int server_was_split=YES;
int got_server_pong;
time_t server_split_time;
#endif

struct Channel *channel = NullChn;

static  void    add_invite (struct Client *, struct Channel *);
static  int     add_banid (struct Client *, struct Channel *, char *);
static  int     add_exceptid(struct Client *, struct Channel *, char *);
static  int     can_join (struct Client *, struct Channel *, char *,int *);
static  int     del_banid (struct Channel *, char *);
static  int     del_exceptid (struct Channel *, char *);
static  void    free_bans_exceptions_denies(struct Channel *);
static  void    free_a_ban_list(Link *ban_ptr);
static  int     is_banned (struct Client *, struct Channel *);
static  void    sub1_from_channel (struct Channel *);


/* static functions used in set_mode */
static char* pretty_mask(char *);
static char *fix_key(char *);
static char *fix_key_old(char *);
static void collapse_signs(char *);
static int errsent(int,int *);
static void change_chan_flag(struct Channel *, struct Client *, int );
static void set_deopped(struct Client *,struct Channel *,int);

static  char    *PartFmt = ":%s PART %s";
/*
 * some buffers for rebuilding channel/nick lists with ,'s
 */

static  char    buf[BUFSIZE];
static  char    modebuf[MODEBUFLEN], modebuf2[MODEBUFLEN];
static  char    parabuf[MODEBUFLEN], parabuf2[MODEBUFLEN];


/* 
 * return the length (>=0) of a chain of links.
 */
static  int     list_length(Link *lp)
{
  int   count = 0;

  for (; lp; lp = lp->next)
    count++;
  return count;
}

/*
 *  Fixes a string so that the first white space found becomes an end of
 * string marker (`\0`).  returns the 'fixed' string or "*" if the string
 * was NULL length or a NULL pointer.
 */
static char* check_string(char* s)
{
  static char star[2] = "*";
  char* str = s;

  if (BadPtr(s))
    return star;

  for ( ; *s; ++s) {
    if (IsSpace(*s))
      {
        *s = '\0';
        break;
      }
  }
  return str;
}

/*
 * create a string of form "foo!bar@fubar" given foo, bar and fubar
 * as the parameters.  If NULL, they become "*".
 */
static char* make_nick_user_host(const char* nick, 
                                 const char* name, const char* host)
{
  static char namebuf[NICKLEN + USERLEN + HOSTLEN + 6];
  int   n;
  char* s;
  const char* p;

  s = namebuf;

  for (p = nick, n = NICKLEN; *p && n--; )
    *s++ = *p++;
  *s++ = '!';
  for(p = name, n = USERLEN; *p && n--; )
    *s++ = *p++;
  *s++ = '@';
  for(p = host, n = HOSTLEN; *p && n--; )
    *s++ = *p++;
  *s = '\0';
  return namebuf;
}

/*
 * Ban functions to work with mode +b
 */
/* add_banid - add an id to be banned to the channel  (belongs to cptr) */

static  int     add_banid(struct Client *cptr, struct Channel *chptr, char *banid)
{
  Link  *ban;

/* Deny users from banning IRC Operators. -malign */ /* I must work on this later! -malign */
  /*if (IsEmpowered(sptr))
   {
    sendto_one(cptr,"Unable to ban IRC Operators.");
    sendto_realops("USER %s (%s@%s) attempted to ban %s (%s@%s) from channel %s.",
                    sptr->name,sptr->username,sptr->host,cptr->name,cptr->username,cptr->host,chptr->chname);
    return -1;
   }
*/

  /* dont let local clients overflow the banlist */
  if ((!IsServer(cptr)) && IsEmpowered(cptr) && (chptr->num_bed >= MAXBANS))
	  if (MyClient(cptr))
	    {
	      sendto_one(cptr, form_str(ERR_BANLISTFULL),
                   me.name, cptr->name,
                   chptr->chname, banid);
	      return -1;
	    }

  if (MyClient(cptr))
    collapse(banid);

  for (ban = chptr->banlist; ban; ban = ban->next)
	  if (match(BANSTR(ban), banid))
	    return -1;

  ban = make_link();
  memset(ban, 0, sizeof(Link));
  ban->flags = CHFL_BAN;
  ban->next = chptr->banlist;

#ifdef BAN_INFO

  ban->value.banptr = (aBan *)MyMalloc(sizeof(aBan));
  ban->value.banptr->banstr = (char *)MyMalloc(strlen(banid)+1);
  (void)strcpy(ban->value.banptr->banstr, banid);

#ifdef USE_UH
  if (IsPerson(cptr))
    {
      ban->value.banptr->who =
        (char *)MyMalloc(strlen(cptr->name)+
                         strlen(cptr->username)+
                         strlen(cptr->host)+3);
      ircsprintf(ban->value.banptr->who, "%s!%s@%s",
                 cptr->name, cptr->username, cptr->host);
    }
  else
    {
#endif
      ban->value.banptr->who = (char *)MyMalloc(strlen(cptr->name)+1);
      (void)strcpy(ban->value.banptr->who, cptr->name);
#ifdef USE_UH
    }
#endif

  ban->value.banptr->when = CurrentTime;

#else

  ban->value.cp = (char *)MyMalloc(strlen(banid)+1);
  (void)strcpy(ban->value.cp, banid);

#endif  /* #ifdef BAN_INFO */

  chptr->banlist = ban;
  chptr->num_bed++;
  return 0;
}

/* add_exceptid - add an id to the exception list for the channel  
 * (belongs to cptr) 
 */

static  int     add_exceptid(struct Client *cptr, struct Channel *chptr, char *eid)
{
  Link  *ex, *ban;

  /* dont let local clients overflow the banlist */
  if ((!IsServer(cptr)) && (chptr->num_bed >= MAXBANS))
    if (MyClient(cptr))
      {
        sendto_one(cptr, form_str(ERR_BANLISTFULL),
                   me.name, cptr->name,
                   chptr->chname, eid);
        return -1;
      }

  if (MyClient(cptr))
    (void)collapse(eid);

  for (ban = chptr->exceptlist; ban; ban = ban->next)
	  if (match(BANSTR(ban), eid))
	    return -1;

  ex = make_link();
  memset(ex, 0, sizeof(Link));
  ex->flags = CHFL_EXCEPTION;
  ex->next = chptr->exceptlist;

#ifdef BAN_INFO

  ex->value.banptr = (aBan *)MyMalloc(sizeof(aBan));
  ex->value.banptr->banstr = (char *)MyMalloc(strlen(eid)+1);
  (void)strcpy(ex->value.banptr->banstr, eid);

#ifdef USE_UH
  if (IsPerson(cptr))
    {
      ex->value.banptr->who =
        (char *)MyMalloc(strlen(cptr->name)+
                         strlen(cptr->username)+
                         strlen(cptr->host)+3);
      ircsprintf(ex->value.banptr->who, "%s!%s@%s",
                 cptr->name, cptr->username, cptr->host);
    }
  else
    {
#endif
      ex->value.banptr->who = (char *)MyMalloc(strlen(cptr->name)+1);
      (void)strcpy(ex->value.banptr->who, cptr->name);
#ifdef USE_UH
    }
#endif

  ex->value.banptr->when = CurrentTime;

#else

  ex->value.cp = (char *)MyMalloc(strlen(eid)+1);
  (void)strcpy(ex->value.cp, eid);

#endif  /* #ifdef BAN_INFO */

  chptr->exceptlist = ex;
  chptr->num_bed++;
  return 0;
}

/*
 *
 * "del_banid - delete an id belonging to cptr
 * if banid is null, deleteall banids belonging to cptr."
 *
 * from orabidoo
 */
static  int     del_banid(struct Channel *chptr, char *banid)
{
  Link **ban;
  Link *tmp;

  if (!banid)
    return -1;
  for (ban = &(chptr->banlist); *ban; ban = &((*ban)->next))
#ifdef BAN_INFO
    if (irccmp(banid, (*ban)->value.banptr->banstr)==0)
#else
      if (irccmp(banid, (*ban)->value.cp)==0)
#endif
        {
          tmp = *ban;
          *ban = tmp->next;
#ifdef BAN_INFO
          MyFree(tmp->value.banptr->banstr);
          MyFree(tmp->value.banptr->who);
          MyFree(tmp->value.banptr);
#else
          MyFree(tmp->value.cp);
#endif
          free_link(tmp);
	  /* num_bed should never be < 0 */
	  if(chptr->num_bed > 0)
	    chptr->num_bed--;
	  else
	    chptr->num_bed = 0;
          break;
        }
  return 0;
}

/*
 * del_exceptid - delete an id belonging to cptr
 *
 * from orabidoo
 */
static  int     del_exceptid(struct Channel *chptr, char *eid)
{
  Link **ex;
  Link *tmp;

  if (!eid)
    return -1;
  for (ex = &(chptr->exceptlist); *ex; ex = &((*ex)->next))
    if (irccmp(eid, BANSTR(*ex)) == 0)
      {
        tmp = *ex;
        *ex = tmp->next;
#ifdef BAN_INFO
        MyFree(tmp->value.banptr->banstr);
        MyFree(tmp->value.banptr->who);
        MyFree(tmp->value.banptr);
#else
        MyFree(tmp->value.cp);
#endif
        free_link(tmp);
	/* num_bed should never be < 0 */
	if(chptr->num_bed > 0)
	  chptr->num_bed--;
	else
	  chptr->num_bed = 0;
        break;
      }
  return 0;
}

/*
 * del_matching_exception - delete an exception matching this user
 *
 * The idea is, if a +e client gets kicked for any reason
 * remove the matching exception for this client.
 * This will immediately stop channel "chatter" with scripts
 * that kick on matching ban. It will also stop apparent "desyncs."
 * It's not the long term answer, but it will tide us over.
 *
 * modified from orabidoo - Dianora
 */
static void del_matching_exception(struct Client *cptr,struct Channel *chptr)
{
  Link **ex;
  Link *tmp;
  char  s[NICKLEN + USERLEN + HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return;

  strcpy(s, make_nick_user_host(cptr->name, cptr->username, cptr->host));
  s2 = make_nick_user_host(cptr->name, cptr->username,
                           inetntoa((char*) &cptr->ip));

  for (ex = &(chptr->exceptlist); *ex; ex = &((*ex)->next))
    {
      tmp = *ex;

      if (match(BANSTR(tmp), s) ||
          match(BANSTR(tmp), s2) )
        {

          /* code needed here to send -e to channel.
           * I will not propogate the removal,
           * This will lead to desyncs of e modes,
           * but its not going to be any worse then it is now.
           *
           * Kickee gets to see the -e removal by the server
           * since they have not yet been removed from the channel.
           * I don't think thats a biggie.
           *
           * -Dianora
           */

#ifdef HIDE_OPS
          sendto_channel_chanops_butserv(chptr,
                                 &me,
                                 ":%s MODE %s -e %s", 
                                 me.name,
                                 chptr->chname,
                                 BANSTR(tmp));
#else
          sendto_channel_butserv(chptr,
                                 &me,
                                 ":%s MODE %s -e %s", 
                                 me.name,
                                 chptr->chname,
                                 BANSTR(tmp));
#endif

          *ex = tmp->next;
#ifdef BAN_INFO
          MyFree(tmp->value.banptr->banstr);
          MyFree(tmp->value.banptr->who);
          MyFree(tmp->value.banptr);
#else
          MyFree(tmp->value.cp);
#endif
          free_link(tmp);
	  /* num_bed should never be < 0 */
	  if(chptr->num_bed > 0)
	    chptr->num_bed--;
	  else
	    chptr->num_bed = 0;
          return;
        }
    }
}

/*
 * is_banned -  returns an int 0 if not banned,
 *              CHFL_BAN if banned
 *              CHFL_EXCEPTION if they have a ban exception
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */

static  int is_banned(struct Client *cptr,struct Channel *chptr)
{
  Link *tmp;
  char  s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return (0);

  strcpy(s, make_nick_user_host(cptr->name, cptr->username, cptr->host));
  s2 = make_nick_user_host(cptr->name, cptr->username,
                           inetntoa((char*) &cptr->ip));

  for (tmp = chptr->banlist; tmp; tmp = tmp->next)
    if (match(BANSTR(tmp), s) ||
        match(BANSTR(tmp), s2))
      break;

#ifdef CHANMODE_E
  if (tmp)
    {
      Link *t2;
      for (t2 = chptr->exceptlist; t2; t2 = t2->next)
        if (match(BANSTR(t2), s) ||
            match(BANSTR(t2), s2))
          {
            return CHFL_EXCEPTION;
          }
    }
#endif

  /* return CHFL_BAN for +b or +d match, we really dont need to be more
     specific */
  return ((tmp?CHFL_BAN:0));
}

/*
 * adds a user to a channel by adding another link to the channels member
 * chain.
 */
void    add_user_to_channel(struct Channel *chptr, struct Client *who, int flags)
{
  Link *ptr;

  if (who->user)
    {
      ptr = make_link();
      ptr->flags = flags;
      ptr->value.cptr = who;
      ptr->next = chptr->members;
      chptr->members = ptr;

      chptr->users++;

      ptr = make_link();
      ptr->value.chptr = chptr;
      ptr->next = who->user->channel;
      who->user->channel = ptr;
      who->user->joined++;
    }
}

void    remove_user_from_channel(struct Client *sptr,struct Channel *chptr,int was_kicked)
{
  Link  **curr;
  Link  *tmp;

  for (curr = &chptr->members; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.cptr == sptr)
      {
        /* User was kicked, but had an exception.
         * so, to reduce chatter I'll remove any
         * matching exception now.
         */
        if(was_kicked && (tmp->flags & CHFL_EXCEPTION))
          {
            del_matching_exception(sptr,chptr);
          }
        *curr = tmp->next;
        free_link(tmp);
        break;
      }
  for (curr = &sptr->user->channel; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
        *curr = tmp->next;
        free_link(tmp);
        break;
      }
  sptr->user->joined--;

  sub1_from_channel(chptr);

}

static  void    change_chan_flag(struct Channel *chptr,struct Client *cptr, int flag)
{
  Link *tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
   {
    if (flag & MODE_ADD)
      {
        tmp->flags |= flag & MODE_FLAGS;
        if (flag & MODE_CHANOP)
          tmp->flags &= ~MODE_DEOPPED;
      }
    else
      {
        tmp->flags &= ~flag & MODE_FLAGS;
      }
   }
}

static  void    set_deopped(struct Client *cptr, struct Channel *chptr,int flag)
{
  Link  *tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
    if ((tmp->flags & flag) == 0)
      tmp->flags |= MODE_DEOPPED;
}

/* int     is_admin(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_ADMIN);

  return 0;
} */

int     is_chan_op(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_CHANOP);
  
  return 0;
}

int     is_deopped(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_DEOPPED);
  
  return 0;
}

int     has_voice(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_VOICE);

  return 0;
}

int     can_send(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

#ifdef JUPE_CHANNEL
  if (MyClient(cptr) && chptr->juped)
    {
      return 1;
    }
#endif

  lp = find_user_link(chptr->members, cptr);

  if (chptr->mode.mode & MODE_MODERATED &&
      /* (!lp || !(lp->flags & (CHFL_ADMIN|CHFL_CHANOP|CHFL_VOICE)))) */
      (!lp || !(lp->flags & (CHFL_CHANOP|CHFL_VOICE))))
    return (MODE_MODERATED);

  if (chptr->mode.mode & MODE_NOPRIVMSGS && !lp)
    return (MODE_NOPRIVMSGS);

  return 0;
}

int     user_channel_mode(struct Client *cptr, struct Channel *chptr)
{
  Link  *lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags);
  
  return 0;
}

/*
 * write the "simple" list of channel modes for channel chptr onto buffer mbuf
 * with the parameters in pbuf.
 */
void channel_modes(struct Client *cptr, char *mbuf, char *pbuf, struct Channel *chptr)
{
  *mbuf++ = '+';

  if (chptr->mode.mode & MODE_SECRET)
    *mbuf++ = 's';

  if (chptr->mode.mode & MODE_PRIVATE)
    *mbuf++ = 'p';

#ifdef SERVICES
  if (chptr->mode.mode & MODE_REGISTERED)
    *mbuf++ = 'r';
#endif /* SERVICES */

  if (chptr->mode.mode & MODE_MODERATED)
    *mbuf++ = 'm';
  if (chptr->mode.mode & MODE_TOPICLIMIT)
    *mbuf++ = 't';
  if (chptr->mode.mode & MODE_INVITEONLY)
    *mbuf++ = 'i';
  if (chptr->mode.mode & MODE_NOPRIVMSGS)
    *mbuf++ = 'n';
  if (chptr->mode.limit)
    {
      *mbuf++ = 'l';
      if (IsMember(cptr, chptr) || IsServer(cptr))
        ircsprintf(pbuf, "%d ", chptr->mode.limit);
    }
  if (*chptr->mode.key)
    {
      *mbuf++ = 'k';
      if (IsMember(cptr, chptr) || IsServer(cptr))
        (void)strcat(pbuf, chptr->mode.key);
    }
  *mbuf++ = '\0';
  return;
}

/*
 * only used to send +b and +e now, +d/+a too.
 * 
 */

static  void    send_mode_list(struct Client *cptr,
                               char *chname,
                               Link *top,
                               int mask,
                               char flag)
{
  Link  *lp;
  char  *cp, *name;
  int   count = 0, dosend = 0;
  
  cp = modebuf + strlen(modebuf);
  if (*parabuf) /* mode +l or +k xx */
    count = 1;
  for (lp = top; lp; lp = lp->next)
    {
      if (!(lp->flags & mask))
        continue;
      name = BANSTR(lp);
        
      if (strlen(parabuf) + strlen(name) + 10 < (size_t) MODEBUFLEN)
        {
          (void)strcat(parabuf, " ");
          (void)strcat(parabuf, name);
          count++;
          *cp++ = flag;
          *cp = '\0';
        }
      else if (*parabuf)
        dosend = 1;
      if (count == 3)
        dosend = 1;
      if (dosend)
        {
          sendto_one(cptr, ":%s MODE %s %s %s",
                     me.name, chname, modebuf, parabuf);
          dosend = 0;
          *parabuf = '\0';
          cp = modebuf;
          *cp++ = '+';
          if (count != 3)
            {
              (void)strcpy(parabuf, name);
              *cp++ = flag;
            }
          count = 0;
          *cp = '\0';
        }
    }
}

/*
 * send "cptr" a full list of the modes for channel chptr.
 */
void send_channel_modes(struct Client *cptr, struct Channel *chptr)
{
  Link  *l, *anop = NULL, *skip = NULL;
  int   n = 0;
  char  *t;

  if (*chptr->chname != '#')
    return;

  *modebuf = *parabuf = '\0';
  channel_modes(cptr, modebuf, parabuf, chptr);

  if (*parabuf)
    strcat(parabuf, " ");
  ircsprintf(buf, ":%s SJOIN %lu %s %s %s:", me.name,
          chptr->channelts, chptr->chname, modebuf, parabuf);
  t = buf + strlen(buf);
  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
        anop = l;
        break;
      }
  /* follow the channel, but doing anop first if it's defined
  **  -orabidoo
  */
  l = NULL;
  for (;;)
    {
      if (anop)
        {
          l = skip = anop;
          anop = NULL;
        }
      else 
        {
          if (l == NULL || l == skip)
            l = chptr->members;
          else
            l = l->next;
          if (l && l == skip)
            l = l->next;
          if (l == NULL)
            break;
        }
      /* if (l->flags & MODE_ADMIN)
        *t++ = '*'; */
      if (l->flags & MODE_CHANOP)
        *t++ = '@';
      if (l->flags & MODE_VOICE)
        *t++ = '+';
      strcpy(t, l->value.cptr->name);
      t += strlen(t);
      *t++ = ' ';
      n++;
      if (t - buf > BUFSIZE - 80)
        {
          *t++ = '\0';
          if (t[-1] == ' ') t[-1] = '\0';
          sendto_one(cptr, "%s", buf);
          ircsprintf(buf, ":%s SJOIN %lu %s 0 :",
                  me.name, chptr->channelts,
                  chptr->chname);
          t = buf + strlen(buf);
          n = 0;
        }
    }
      
  if (n)
    {
      *t++ = '\0';
      if (t[-1] == ' ') t[-1] = '\0';
      sendto_one(cptr, "%s", buf);
    }
  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->banlist, CHFL_BAN,'b');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
               me.name, chptr->chname, modebuf, parabuf);

  if(!IsCapable(cptr,CAP_EX))
    return;

  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->exceptlist, CHFL_EXCEPTION,'e');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
               me.name, chptr->chname, modebuf, parabuf);
}

/* stolen from Undernet's ircd  -orabidoo
 *
 */

static char* pretty_mask(char* mask)
{
  char* cp = mask;
  char* user;
  char* host;

  if ((user = strchr(cp, '!')))
    *user++ = '\0';
  if ((host = strrchr(user ? user : cp, '@')))
    {
      *host++ = '\0';
      if (!user)
        return make_nick_user_host("*", check_string(cp), check_string(host));
    }
  else if (!user && strchr(cp, '.'))
    return make_nick_user_host("*", "*", check_string(cp));
  return make_nick_user_host(check_string(cp), check_string(user), check_string(host));
}

static  char    *fix_key(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
    {
      c &= 0x7f;
      if (c != ':' && c > ' ')
      {
        *t++ = c;
      }
    }
  *t = '\0';
  return arg;
}

/*
 * Here we attempt to be compatible with older non-hybrid servers.
 * We can't back down from the ':' issue however.  --Rodder
 */
static  char    *fix_key_old(char *arg)
{
  u_char        *s, *t, c;

  for (s = t = (u_char *)arg; (c = *s); s++)
    { 
      c &= 0x7f;
      if ((c != 0x0a) && (c != ':'))
        *t++ = c;
    }
  *t = '\0';
  return arg;
}

/*
 * like the name says...  take out the redundant signs in a modechange list
 */
static  void    collapse_signs(char *s)
{
  char  plus = '\0', *t = s, c;
  while ((c = *s++))
    {
      if (c != plus)
        *t++ = c;
      if (c == '+' || c == '-')
        plus = c;
    }
  *t = '\0';
}

/* little helper function to avoid returning duplicate errors */
static  int     errsent(int err, int *errs)
{
  if (err & *errs)
    return 1;
  *errs |= err;
  return 0;
}

/* bitmasks for various error returns that set_mode should only return
 * once per call  -orabidoo
 */

#define SM_ERR_NOTS             0x00000001      /* No TS on channel */
#define SM_ERR_NOOPS            0x00000002      /* No chan ops */
#define SM_ERR_UNKNOWN          0x00000004
#define SM_ERR_RPL_C            0x00000008
#define SM_ERR_RPL_B            0x00000010
#define SM_ERR_RPL_E            0x00000020
#define SM_ERR_NOTONCHANNEL     0x00000040      /* Not on channel */
#define SM_ERR_RESTRICTED       0x00000080      /* Restricted chanop */

/*
** Apply the mode changes passed in parv to chptr, sending any error
** messages and MODE commands out.  Rewritten to do the whole thing in
** one pass, in a desperate attempt to keep the code sane.  -orabidoo
*/
/*
 * rewritten to remove +h/+c/z 
 * in spirit with the one pass idea, I've re-written how "imnspt"
 * handling was done
 *
 * I've also left some "remnants" of the +h code in for possible
 * later addition.
 * For example, isok could be replaced witout half ops, with ischop() or
 * chan_op depending.
 *
 */

void set_channel_mode(struct Client *cptr,
                      struct Client *sptr,
                      struct Channel *chptr,
                      int parc,
                      char *parv[])
{
  int   errors_sent = 0, opcnt = 0, len = 0, tmp, nusers;
  int   keychange = 0, limitset = 0;
  int   whatt = MODE_ADD, the_mode = 0;
  int   done_s = NO, done_p = NO;
  int   done_i = NO, done_m = NO, done_n = NO, done_t = NO;

#ifdef SERVICES
  int done_r = NO;
#endif /* SERVICES */

  struct Client *who;
  Link  *lp;
  char  *curr = parv[0], c, *arg, plus = '+', *tmpc;
  char  numeric[16];
  /* mbufw gets the param-less mode chars, always with their sign
   * mbuf2w gets the paramed mode chars, always with their sign
   * pbufw gets the params, in ID form whenever possible
   * pbuf2w gets the params, no ID's
   */
  /* no ID code at the moment
   * pbufw gets the params, no ID's
   * grrrr for now I'll stick the params into pbufw without ID's
   * -Dianora
   */
  /* *sigh* FOR YOU Roger, and ONLY for you ;-)
   * lets stick mode/params that only the newer servers will understand
   * into modebuf_new/parabuf_new 
   */

  char  modebuf_new[MODEBUFLEN];
  char  parabuf_new[MODEBUFLEN];

  char  *mbufw = modebuf, *mbuf2w = modebuf2;
  char  *pbufw = parabuf, *pbuf2w = parabuf2;

  char  *mbufw_new = modebuf_new;
  char  *pbufw_new = parabuf_new;

  int   ischop;
  int   isok;
  int   isdeop;
  int   chan_op;
  int   user_mode_chan;

  user_mode_chan = user_channel_mode(sptr, chptr);
  chan_op = (user_mode_chan & CHFL_CHANOP);

  /* has ops or is a server */
  ischop = IsServer(sptr) || chan_op || IsAdmin(sptr); /* Added IsAdmin -malign */

  /* is client marked as deopped */
  isdeop = !ischop && !IsServer(sptr) && !IsAdmin(sptr) && (user_mode_chan & CHFL_DEOPPED); /* Added IsAdmin -malign */

  /* is an op or server or remote user on a TS channel */
  isok = ischop || IsAdmin(sptr) || (!isdeop && IsServer(cptr) && chptr->channelts); /* Added IsAdmin -malign */

  /* isok_c calculated later, only if needed */

  /* parc is the number of _remaining_ args (where <0 means 0);
  ** parv points to the first remaining argument
  */
  parc--;
  parv++;

  for ( ; ; )
    {
      if (BadPtr(curr))
        {
          /*
           * Deal with mode strings like "+m +o blah +i"
           */
          if (parc-- > 0)
            {
              curr = *parv++;
              continue;
            }
          break;
        }
      c = *curr++;

      switch (c)
        {
        case '+' :
          whatt = MODE_ADD;
          plus = '+';
          continue;
          /* NOT REACHED */
          break;

        case '-' :
          whatt = MODE_DEL;
          plus = '-';
          continue;
          /* NOT REACHED */
          break;

        case '=' :
          whatt = MODE_QUERY;
          plus = '=';   
          continue;
          /* NOT REACHED */
          break;

        /* case 'a' : */
        case 'o' :
        case 'v' :
          if (MyClient(sptr))
            {
              if(!IsMember(sptr, chptr))
                {
                  if(!errsent(SM_ERR_NOTONCHANNEL, &errors_sent))
                    sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                               me.name, sptr->name, chptr->chname);
                  /* eat the parameter */
                  parc--;
                  parv++;
                  break;
                }
#ifdef LITTLE_I_LINES
              else
                {
                  if(IsRestricted(sptr) && (whatt == MODE_ADD))
                    {
                      if(!errsent(SM_ERR_RESTRICTED, &errors_sent))
                        {
                          sendto_one(sptr,
            ":%s NOTICE %s :*** Notice -- You are restricted and cannot chanop others",
                                 me.name,
                                 sptr->name);
                        }
                      /* eat the parameter */
                      parc--;
                      parv++;
                      break;
                    }
                }
#endif
            }
          if (whatt == MODE_QUERY)
            break;
          if (parc-- <= 0)
            break;
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!(who = find_chasing(sptr, arg, NULL)))
            break;

          /* there is always the remote possibility of picking up
           * a bogus user, be nasty to core for that. -Dianora
           */

          if (!who->user)
            break;

          /* no more of that mode bouncing crap */
          if (!IsMember(who, chptr))
            {
              if (MyClient(sptr))
                sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL), me.name, 
                           sptr->name, arg, chptr->chname);
              break;
            }

           /* Allow opers to set +o mode without chanop -malign */
        /*  if ((who == sptr) && (c == 'o'))
            {
              if(whatt == MODE_ADD)
                break;
	    } */

if ((who == sptr) && (c == 'o'))
 {
       if(whatt == MODE_ADD && IsAdmin(sptr))
      /* Send message to opers stating an oper opped himself in a channel -malign */
      /*if (IsEmpowered(sptr) && !chan_op && !IsLocOp(sptr))*/  /* Do not let local operators op themselves in channels they do not have ops -malign */
       if (IsAdmin(sptr) && !chan_op)
       {
        /*sendto_realops("OPER %s (%s@%s) set mode +o on %s without ChanOp.",sptr->name,sptr->username,sptr->host,chptr->chname);*/
        /* Log mode setting by IRC Operators without privileges -malign */
        Log("OPER %s (%s@%s) set mode +o on %s without ChanOp.",sptr->name,sptr->username,sptr->host,chptr->chname);
       }
 }

          /* ignore server-generated MODE +ovh */
  /*        if (IsServer(sptr) && (whatt == MODE_ADD))
            {
              ts_warn( "MODE +%c on %s for %s from server %s (ignored)", 
                       c, chptr->chname, 
                       who->name,sptr->name);
              break;
            }
*/
          /* if (c == 'a')
            the_mode = MODE_ADMIN; */
          if (c == 'o')
            the_mode = MODE_CHANOP;
          else if (c == 'v')
            the_mode = MODE_VOICE;

          if (isdeop && (c == 'o') && whatt == MODE_ADD)
             set_deopped(who, chptr, the_mode);

          /* Only allow the admin to +a/-a themself -malign */
          /* if(the_mode == MODE_ADMIN && whatt == MODE_DEL)
             {
              if (IsAdmin(sptr) && IsAdmin(who) && sptr->name == who->name)
                {
                  sendto_channel_butserv(chptr, sptr, ":%s MODE %s -a %s",sptr->name,chptr->chname,who->name);
                  sendto_match_servs(chptr, cptr, ":%s MODE %s -a %s",sptr->name,chptr->chname,who->name);
                  break;
                }
              else
                {
              sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name,
              sptr->name, chptr->chname);
              break;
                }
             } */

          /* if(the_mode == MODE_ADMIN && whatt == MODE_ADD)
             {
              if (IsAdmin(sptr) && IsAdmin(who) && sptr->name == who->name)
                {
                 sendto_channel_butserv(chptr, sptr, ":%s MODE %s +a %s",sptr->name,chptr->chname,who->name);
                 sendto_match_servs(chptr, cptr, ":%s MODE %s +a %s",sptr->name,chptr->chname,who->name);
                 break;
                }
              else
                {
                 sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name,
                 sptr->name, chptr->chname);
                 break;
                }
             } */

#ifdef HIDE_OPS
	  if(the_mode == MODE_CHANOP && whatt == MODE_DEL)
	    sendto_one(who,":%s MODE %s -o %s",sptr->name,chptr->chname,who->name);
#endif
        if(the_mode == MODE_CHANOP && whatt == MODE_DEL)
         {
          /* Don't let someone deop an IRC Oper. Only allow them to deop themselves. -malign */
          if (IsAdmin(sptr) && IsAdmin(who) && sptr->name == who->name)
           {
            sendto_channel_butserv(chptr, sptr, ":%s MODE %s -o %s",sptr->name,chptr->chname,who->name);
            sendto_match_servs(chptr, cptr, ":%s MODE %s -o %s",sptr->name,chptr->chname,who->name);
            break;
           }
          if (IsAdmin(who))
           {
            sendto_one(sptr, "Unable to deop IRC Operators.");
            sendto_realops("USER %s (%s@%s) attempted to deop OPER %s (%s@%s) on channel %s.",
                           sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
            /* Log deop IRC Operator attempts -malign */
            Log("USER %s (%s@%s) attempted to deop OPER %s (%s@%s) on channel %s.",sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
            break;
           }

         /* Allow IRC Operators to deop other IRC Operators in chans. -malign */
         /* else if (IsEmpowered(who) && IsEmpowered(sptr))
           {
            sendto_realops("OPER %s (%s@%s) deopped OPER %s (%s@%s) on channel %s.",
                           sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
           }*/
         }

          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }
        
          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          *mbufw++ = plus;
          *mbufw++ = c;
          strcpy(pbufw, who->name);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          opcnt++;

          change_chan_flag(chptr, who, the_mode|whatt);

          break;

        case 'k':
          if (whatt == MODE_QUERY)
            break;
          if (parc-- <= 0)
            {
              /* allow arg-less mode -k */
              if (whatt == MODE_DEL)
                arg = "*";
              else
                break;
            }
          else
            {
              if (whatt == MODE_DEL)
                {
                  arg = check_string(*parv++);
                }
              else
                {
                  if MyClient(sptr)
                    arg = fix_key(check_string(*parv++));
                  else
                    arg = fix_key_old(check_string(*parv++));
                }
            }

          if (keychange++)
            break;
          /*      if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;
            */
          if (!*arg)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if ( (tmp = strlen(arg)) > KEYLEN)
            {
              arg[KEYLEN] = '\0';
              tmp = KEYLEN;
            }

          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          /* if there is already a key, and the client is adding one
           * remove the old one, then add the new one
           */

          if((whatt == MODE_ADD) && *chptr->mode.key)
            {
              /* If the key is the same, don't do anything */

              if(!strcmp(chptr->mode.key,arg))
                break;

              sendto_channel_butserv(chptr, sptr, ":%s MODE %s -k %s", 
                                     sptr->name, chptr->chname,
                                     chptr->mode.key);

              sendto_match_servs(chptr, cptr, ":%s MODE %s -k %s",
                                 sptr->name, chptr->chname,
                                 chptr->mode.key);
            }

          if (whatt == MODE_DEL)
            {
              if( (arg[0] == '*') && (arg[1] == '\0'))
                arg = chptr->mode.key;
              else
                {
                  if(strcmp(arg,chptr->mode.key))
                    break;
		}
	    }

          *mbufw++ = plus;
          *mbufw++ = 'k';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          /*      opcnt++; */

          if (whatt == MODE_DEL)
            {
              *chptr->mode.key = '\0';
            }
          else
            {
              /*
               * chptr was zeroed
               */
              strncpy_irc(chptr->mode.key, arg, KEYLEN);
            }

          break;

          /* There is a nasty here... I'm supposed to have
           * CAP_EX before I can send exceptions to bans to a server.
           * But that would mean I'd have to keep two strings
           * one for local clients, and one for remote servers,
           * one with the 'e' strings, one without.
           * I added another parameter buf and mode buf for "new"
           * capabilities.
           *
           * -Dianora
           */

        case 'e':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;
              if (errsent(SM_ERR_RPL_E, &errors_sent))
                break;
              /* don't allow a non chanop to see the exception list
               * suggested by Matt on operlist nov 25 1998
               */
              if(isok)
                {
#ifdef BAN_INFO
                  for (lp = chptr->exceptlist; lp; lp = lp->next)
                    sendto_one(cptr, form_str(RPL_EXCEPTLIST),
                               me.name, cptr->name,
                               chptr->chname,
                               lp->value.banptr->banstr,
                               lp->value.banptr->who,
                               lp->value.banptr->when);
#else 
                  for (lp = chptr->exceptlist; lp; lp = lp->next)
                    sendto_one(cptr, form_str(RPL_EXCEPTLIST),
                               me.name, cptr->name,
                               chptr->chname,
                               lp->value.cp);
#endif
                  sendto_one(sptr, form_str(RPL_ENDOFEXCEPTLIST),
                             me.name, sptr->name, 
                             chptr->chname);
                }
              else
                {
                  sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                               sptr->name, chptr->chname);
                }
              break;
            }
          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chptr->chname);
              break;
            }
          
          /* user-friendly ban mask generation, taken
          ** from Undernet's ircd  -orabidoo
          */
          if (MyClient(sptr))
            arg = collapse(pretty_mask(arg));

          if(*arg == ':')
            {
              parc--;
              parv++;
              break;
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

#ifndef CHANMODE_E
	  if(whatt == MODE_ADD)
	    break;
#endif

          if (!(((whatt & MODE_ADD) && !add_exceptid(sptr, chptr, arg)) ||
                ((whatt & MODE_DEL) && !del_exceptid(chptr, arg))))
            break;

          /* This stuff can go back in when all servers understand +e 
           * with the pbufw_new nonsense removed -Dianora
           */

          /*
          *mbufw++ = plus;
          *mbufw++ = 'e';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          */
          len += tmp + 1;
          opcnt++;

          *mbufw_new++ = plus;
          *mbufw_new++ = 'e';
          strcpy(pbufw_new, arg);
          pbufw_new += strlen(pbufw_new);
          *pbufw_new++ = ' ';

          break;

        /* +d removed ... properly -gnp */

        case 'b':
          if (whatt == MODE_QUERY || parc-- <= 0)
            {
              if (!MyClient(sptr))
                break;

              if (errsent(SM_ERR_RPL_B, &errors_sent))
                break;
#ifdef BAN_INFO
#ifdef HIDE_OPS
	      if(chan_op)
#endif
		{
		  for (lp = chptr->banlist; lp; lp = lp->next)
		    sendto_one(cptr, form_str(RPL_BANLIST),
			       me.name, cptr->name,
			       chptr->chname,
			       lp->value.banptr->banstr,
			       lp->value.banptr->who,
			       lp->value.banptr->when);
		}
#ifdef HIDE_OPS
	      else
		{
		  for (lp = chptr->banlist; lp; lp = lp->next)
		    sendto_one(cptr, form_str(RPL_BANLIST),
			       me.name, cptr->name,
			       chptr->chname,
			       lp->value.banptr->banstr,
			       "*",0);
		}
#endif
#else 
              for (lp = chptr->banlist; lp; lp = lp->next)
                sendto_one(cptr, form_str(RPL_BANLIST),
                           me.name, cptr->name,
                           chptr->chname,
                           lp->value.cp);
#endif
              sendto_one(sptr, form_str(RPL_ENDOFBANLIST),
                         me.name, sptr->name, 
                         chptr->chname);
              break;
            }

          arg = check_string(*parv++);

          if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
            break;

          if (!isok)
            {
              if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                           me.name, sptr->name, 
                           chptr->chname);
              break;
            }


          /* Ignore colon at beginning of ban string.
           * Unfortunately, I can't ignore all such strings,
           * because otherwise the channel could get desynced.
           * I can at least, stop local clients from placing a ban
           * with a leading colon.
           *
           * Roger uses check_string() combined with an earlier test
           * in his TS4 code. The problem is, this means on a mixed net
           * one can't =remove= a colon prefixed ban if set from
           * an older server.
           * His code is more efficient though ;-/ Perhaps
           * when we've all upgraded this code can be moved up.
           *
           * -Dianora
           */

          /* user-friendly ban mask generation, taken
          ** from Undernet's ircd  -orabidoo
          */
          if (MyClient(sptr))
            {
              if( (*arg == ':') && (whatt & MODE_ADD) )
                {
                  parc--;
                  parv++;
                  break;
                }
              arg = collapse(pretty_mask(arg));
            }

          tmp = strlen(arg);
          if (len + tmp + 2 >= MODEBUFLEN)
            break;

          if (!(((whatt & MODE_ADD) && !add_banid(sptr, chptr, arg)) ||
                ((whatt & MODE_DEL) && !del_banid(chptr, arg))))
            break;

          *mbufw++ = plus;
          *mbufw++ = 'b';
          strcpy(pbufw, arg);
          pbufw += strlen(pbufw);
          *pbufw++ = ' ';
          len += tmp + 1;
          opcnt++;

          break;

        case 'l':
          if (whatt == MODE_QUERY)
            break;
          if (!isok || limitset++)
            {
              if (whatt == MODE_ADD && parc-- > 0)
                parv++;
              break;
            }

          if (whatt == MODE_ADD)
            {
              if (parc-- <= 0)
                {
                  if (MyClient(sptr))
                    sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                               me.name, sptr->name, "MODE +l");
                  break;
                }
              
              arg = check_string(*parv++);
              /*              if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
                break; */
              if ((nusers = atoi(arg)) <= 0)
                break;
              ircsprintf(numeric, "%d", nusers);
              if ((tmpc = strchr(numeric, ' ')))
                *tmpc = '\0';
              arg = numeric;

              tmp = strlen(arg);
              if (len + tmp + 2 >= MODEBUFLEN)
                break;

              chptr->mode.limit = nusers;
              chptr->mode.mode |= MODE_LIMIT;

              *mbufw++ = '+';
              *mbufw++ = 'l';
              strcpy(pbufw, arg);
              pbufw += strlen(pbufw);
              *pbufw++ = ' ';
              len += tmp + 1;
              /*              opcnt++;*/
            }
          else
            {
              chptr->mode.limit = 0;
              chptr->mode.mode &= ~MODE_LIMIT;
              *mbufw++ = '-';
              *mbufw++ = 'l';
            }

          break;

          /* Traditionally, these are handled separately
           * but I decided to combine them all into this one case
           * statement keeping it all sane
           *
           * The disadvantage is a lot more code duplicated ;-/
           *
           * -Dianora
           */

        case 'i' :
          if (whatt == MODE_QUERY)      /* shouldn't happen. */
            break;
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_i)
                break;
              else
                done_i = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;

	      chptr->mode.mode |= MODE_INVITEONLY;
	      *mbufw++ = '+';
	      *mbufw++ = 'i';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;

              while ( (lp = chptr->invites) )
                del_invite(lp->value.cptr, chptr);

	      chptr->mode.mode &= ~MODE_INVITEONLY;
	      *mbufw++ = '-';
	      *mbufw++ = 'i';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

          /* Un documented for now , I have no idea how this got here ;-) */
#ifdef JUPE_CHANNEL
        case 'j':
          if(MyConnect(sptr) && IsEmpowered(sptr))
            {
              if (whatt == MODE_ADD)
                {
                  chptr->juped = 1;
                  sendto_realops("%s!%s@%s locally juping channel %s",
                                 sptr->name, sptr->username,
                                 sptr->host, chptr->chname);
                }
              else if(whatt == MODE_DEL)
                {
                  chptr->juped = 0;
                  sendto_realops("%s!%s@%s locally unjuping channel %s",
                                 sptr->name, sptr->username,
                                 sptr->host, chptr->chname);
		  if(chptr->users == 0)
		    sub1_from_channel(chptr);
                }
            }
          break;
#endif

        case 'm' :
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_m)
                break;
              else
                done_m = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_MODERATED;
	      *mbufw++ = '+';
	      *mbufw++ = 'm';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_MODERATED;
	      *mbufw++ = '-';
	      *mbufw++ = 'm';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 'n' :
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_n)
                break;
              else
                done_n = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_NOPRIVMSGS;
	      *mbufw++ = '+';
	      *mbufw++ = 'n';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_NOPRIVMSGS;
	      *mbufw++ = '-';
	      *mbufw++ = 'n';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 'p' :
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_p)
                break;
              else
                done_p = YES;
              /*              if ( opcnt >= MAXMODEPARAMS)
                break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_PRIVATE;
	      *mbufw++ = '+';
	      *mbufw++ = 'p';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_PRIVATE;
	      *mbufw++ = '-';
	      *mbufw++ = 'p';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        case 's' :
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          /* ickity poo, traditional +p-s nonsense */

          if(MyClient(sptr))
            {
              if(done_s)
                break;
              else
                done_s = YES;
              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.mode |= MODE_SECRET;
              *mbufw++ = '+';
              *mbufw++ = 's';
              len += 2;
              /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.mode &= ~MODE_SECRET;
              *mbufw++ = '-';
              *mbufw++ = 's';
              len += 2;
              /*              opcnt++; */
            }
          break;

#ifdef SERVICES
        case 'r' :
         /*  if (!isok) */ /* Need to replace isok later once IgniteServ is in production to isserv -malign */
           if (!IsServer(cptr))
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
              /*  sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name,
                           sptr->name, chptr->chname); */
                 sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name,
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_r)
                break;
              else
                done_r = YES;
              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.mode |= MODE_REGISTERED;
              *mbufw++ = '+';
              *mbufw++ = 'r';
              len += 2;
              /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
              chptr->mode.mode &= ~MODE_REGISTERED;
              *mbufw++ = '-';
              *mbufw++ = 'r';
              len += 2;
              /*              opcnt++; */
            }
          break;
#endif /* SERVICES */

        case 't' :
          if (!isok)
            {
              if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
                sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED), me.name, 
                           sptr->name, chptr->chname);
              break;
            }

          if(MyClient(sptr))
            {
              if(done_t)
                break;
              else
                done_t = YES;

              /*              if ( opcnt >= MAXMODEPARAMS)
                              break; */
            }

          if(whatt == MODE_ADD)
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode |= MODE_TOPICLIMIT;
	      *mbufw++ = '+';
	      *mbufw++ = 't';
	      len += 2;
	      /*              opcnt++; */
            }
          else
            {
              if (len + 2 >= MODEBUFLEN)
                break;
	      chptr->mode.mode &= ~MODE_TOPICLIMIT;
	      *mbufw++ = '-';
	      *mbufw++ = 't';
	      len += 2;
	      /*              opcnt++; */
            }
          break;

        default:
          if (whatt == MODE_QUERY)
            break;

          /* only one "UNKNOWNMODE" per mode... we don't want
          ** to generate a storm, even if it's just to a 
          ** local client  -orabidoo
          */
          if (MyClient(sptr) && !errsent(SM_ERR_UNKNOWN, &errors_sent))
            sendto_one(sptr, form_str(ERR_UNKNOWNMODE), me.name, sptr->name, c);
          break;
        }
    }

  /*
  ** WHEW!!  now all that's left to do is put the various bufs
  ** together and send it along.
  */

  *mbufw = *mbuf2w = *pbufw = *pbuf2w = *mbufw_new = *pbufw_new = '\0';

  collapse_signs(modebuf);
  collapse_signs(modebuf_new);

  if(*modebuf)
    {
#ifdef HIDE_OPS
      sendto_channel_chanops_butserv(chptr, sptr, ":%s MODE %s %s %s", 
				     sptr->name, chptr->chname,
				     modebuf, parabuf);
#else
      sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", 
                           sptr->name, chptr->chname,
                           modebuf, parabuf);
#endif
      sendto_match_servs(chptr, cptr, ":%s MODE %s %s %s",
                         sptr->name, chptr->chname,
                         modebuf, parabuf);
    }

  if(*modebuf_new)
    {
      sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", 
                             sptr->name, chptr->chname,
                             modebuf_new, parabuf_new);

      sendto_match_cap_servs(chptr, cptr, CAP_EX, ":%s MODE %s %s %s",
                             sptr->name, chptr->chname,
                             modebuf_new, parabuf_new);
    }
                     
  return;
}

static  int     can_join(struct Client *sptr, struct Channel *chptr, char *key, int *flags)
{
  Link  *lp;
  int ban_or_exception;

#ifdef JUPE_CHANNEL
  if(chptr->juped)
    {
      sendto_ops_flags(FLAGS_SPY,
             "User %s (%s@%s) is attempting to join locally juped channel %s",
                     sptr->name,
                     sptr->username, sptr->host,chptr->chname);
      return (ERR_UNAVAILRESOURCE);
    }
#endif

  /*if (IsEmpowered(sptr) && !IsLocOp(sptr)) */ /* Allow IRC Operators to join channels despite modes -malign */
   if (IsAdmin(sptr))
    {
     return 0;
    }

  /* {
    sendto_realops("OPER %s (%s@%s) joined %s against channel modes.",sptr->name,sptr->username,sptr->host,chptr->chname);
    return;
   } */  /* Need to fix this later, because it spams the oper everytime they enter any chan. Need to provide more conditions. -malign */

  if ( (ban_or_exception = is_banned(sptr, chptr)) == CHFL_BAN)
    return (ERR_BANNEDFROMCHAN);
  else
    *flags |= ban_or_exception; /* Mark this client as "charmed" */

  if (chptr->mode.mode & MODE_INVITEONLY)
    {
      for (lp = sptr->user->invited; lp; lp = lp->next)
        if (lp->value.chptr == chptr)
          break;
      if (!lp)
        return (ERR_INVITEONLYCHAN);
    }

  if (*chptr->mode.key && (BadPtr(key) || irccmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);

  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  return 0;
}

/*
 * check_channel_name - check channel name for invalid characters
 * return true (1) if name ok, false (0) otherwise
 */
int check_channel_name(const char* name)
{
  assert(0 != name);
  
  for ( ; *name; ++name) {
    if (!IsChanChar(*name))
      return 0;
  }
  return 1;
}

/*
**  Get Channel block for chname (and allocate a new channel
**  block, if it didn't exist before).
*/
static struct Channel* get_channel(struct Client *cptr, char *chname, int flag)
{
  struct Channel *chptr;
  int   len;

  if (BadPtr(chname))
    return NULL;

  len = strlen(chname);
  if (MyClient(cptr) && len > CHANNELLEN)
    {
      len = CHANNELLEN;
      *(chname + CHANNELLEN) = '\0';
    }
  if ((chptr = hash_find_channel(chname, NULL)))
    return (chptr);

  /*
   * If a channel is created during a split make sure its marked
   * as created locally 
   * Also make sure a created channel has =some= timestamp
   * even if it get over-ruled later on. Lets quash the possibility
   * an ircd coder accidentally blasting TS on channels. (grrrrr -db)
   *
   * Actually, it might be fun to make the TS some impossibly huge value (-db)
   */

  if (flag == CREATE)
    {
      chptr = (struct Channel*) MyMalloc(sizeof(struct Channel) + len + 1);
      memset(chptr, 0, sizeof(struct Channel));
      /*
       * NOTE: strcpy ok here, we have allocated strlen + 1
       */
      strcpy(chptr->chname, chname);
      if (channel)
        channel->prevch = chptr;
      chptr->prevch = NULL;
      chptr->nextch = channel;
      channel = chptr;
      chptr->channelts = CurrentTime;     /* doesn't hurt to set it here */
      add_to_channel_hash_table(chname, chptr);
      Count.chan++;
    }
  return chptr;
}

static  void    add_invite(struct Client *cptr,struct Channel *chptr)
{
  Link  *inv, **tmp;

  del_invite(cptr, chptr);
  /*
   * delete last link in chain if the list is max length
   */
  if (list_length(cptr->user->invited) >= MAXCHANNELSPERUSER)
    {
      /*                This forgets the channel side of invitation     -Vesa
                        inv = cptr->user->invited;
                        cptr->user->invited = inv->next;
                        free_link(inv);
*/
      del_invite(cptr, cptr->user->invited->value.chptr);
 
    }
  /*
   * add client to channel invite list
   */
  inv = make_link();
  inv->value.cptr = cptr;
  inv->next = chptr->invites;
  chptr->invites = inv;
  /*
   * add channel to the end of the client invite list
   */
  for (tmp = &(cptr->user->invited); *tmp; tmp = &((*tmp)->next))
    ;
  inv = make_link();
  inv->value.chptr = chptr;
  inv->next = NULL;
  (*tmp) = inv;
}

/*
 * Delete Invite block from channel invite list and client invite list
 */
void    del_invite(struct Client *cptr,struct Channel *chptr)
{
  Link  **inv, *tmp;

  for (inv = &(chptr->invites); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.cptr == cptr)
      {
        *inv = tmp->next;
        free_link(tmp);
        break;
      }

  for (inv = &(cptr->user->invited); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
        *inv = tmp->next;
        free_link(tmp);
        break;
      }
}

/*
**  Subtract one user from channel (and free channel
**  block, if channel became empty).
*/
static  void    sub1_from_channel(struct Channel *chptr)
{
  Link *tmp;

  if (--chptr->users <= 0)
    {
      chptr->users = 0; /* if chptr->users < 0, make sure it sticks at 0
                         * It should never happen but...
                         */
#ifdef JUPE_CHANNEL
        if(chptr->juped)
          {
            while ((tmp = chptr->invites))
              del_invite(tmp->value.cptr, chptr);
            
#ifdef FLUD
            free_fluders(NULL, chptr);
#endif
          }
        else
#endif
       {
          /*
           * Now, find all invite links from channel structure
           */
          while ((tmp = chptr->invites))
            del_invite(tmp->value.cptr, chptr);

	  /* free all bans/exceptions/denies */
	  free_bans_exceptions_denies( chptr );

          if (chptr->prevch)
            chptr->prevch->nextch = chptr->nextch;
          else
            channel = chptr->nextch;
          if (chptr->nextch)
            chptr->nextch->prevch = chptr->prevch;

#ifdef FLUD
          free_fluders(NULL, chptr);
#endif
          del_from_channel_hash_table(chptr->chname, chptr);
          MyFree((char*) chptr);
          Count.chan--;
        }
    }
}

/*
 * free_bans_exceptions_denies
 *
 * inputs	- pointer to channel structure
 * output	- none
 * side effects	- all bans/exceptions denies are freed for channel
 */

static void free_bans_exceptions_denies(struct Channel *chptr)
{
  free_a_ban_list(chptr->banlist);
  free_a_ban_list(chptr->exceptlist);

  chptr->banlist = chptr->exceptlist = NULL;
  chptr->num_bed = 0;
}

static void
free_a_ban_list(Link *ban_ptr)
{
  Link *ban;
  Link *next_ban;

  for(ban = ban_ptr; ban; ban = next_ban)
    {
      next_ban = ban->next;
#ifdef BAN_INFO
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
#else
      MyFree(ban->value.cp);
#endif
      free_link(ban);
    }
}

#ifdef NEED_SPLITCODE

/*
 * check_still_split()
 *
 * inputs       -NONE
 * output       -NONE
 * side effects -
 * Check to see if the server split timer has expired, if so
 * check to see if there are now a decent number of servers connected
 * and users present, so I can consider this split over.
 *
 * -Dianora
 */

static void check_still_split()
{
  if((server_split_time + SPLITDELAY) < CurrentTime)
    {
      if((Count.server >= SPLITNUM) &&
#ifdef SPLIT_PONG
         (got_server_pong == YES) &&
#endif
         (Count.total >= SPLITUSERS))
        {
          /* server hasn't been split for a while.
           * -Dianora
           */
          server_was_split = NO;
          sendto_ops("Net Rejoined, split-mode deactivated");
          cold_start = NO;
        }
      else
        {
          server_split_time = CurrentTime; /* still split */
          server_was_split = YES;
        }
    }
}
#endif

/* m_fjoin - Forces client to join channel -malign */
/* parv[0] - Sender prefix */
/* parv[1] - Nickname of target client */
/* parv[2] - Channel to join */
int m_fjoin(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{

#ifndef FJOIN
sendto_one(sptr,"FJOIN is not enabled."); /* Inform user issuing command that it is not compiled in */
#else

  struct Client *acptr = NULL;
  struct Channel *chptr = NULL;
  int flags=0;

     /* Ensure we are given the proper number of arguments -malign */
  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "FJOIN");
      return 0;
    }

     /* If the person attempting to use FJOIN is not local or an IRC Admin, don't even bother -malign */
  if (!MyClient(sptr) || !IsAdmin(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

     /* Verify person exists first -malign */
  if (!(acptr = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK), me.name, parv[0], parv[1]);
      return 0;
    }

     /* If client is not mine, bail -malign */
  if (!MyClient(acptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :%s is not local to our server.", me.name, parv[0], acptr->name);
      return 0;
    }

     /* verify the channel is valid -malign */
  if (!check_channel_name(parv[2]))
    {
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
                 me.name, parv[0], (unsigned char *)parv[2]);
      return 0;
    }

  if (!IsChannelName(parv[2]))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

     /* If the target is an IRC Administrator, then bail -malign */
  if (IsAdmin(acptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :Unable to FJOIN IRC Administrators.", me.name, parv[0]);
      return 0;
    }

     /* Ensure user issuing command is an IRC Administrator -malign */
  if (IsAdmin(sptr))
    {
      if(!chptr)
      chptr = get_channel(acptr, parv[2], CREATE);

      if(chptr)
        {
          if (IsMember(acptr, chptr))    /* already a member, ignore this */
            {
             sendto_one(sptr, ":%s NOTICE %s :User is already on that channel.", me.name, parv[0]);
             return 0;
            }
        }
      sendto_one(acptr, ":%s JOIN :%s", parv[1], parv[2]);

      add_user_to_channel(chptr, acptr, flags);

      chptr->channelts = CurrentTime;
      sendto_match_servs(chptr, acptr, ":%s SJOIN %lu %s + :%s", me.name, chptr->channelts, parv[2], parv[1]);

      sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s", parv[1], parv[2]);
      /* sendto_match_servs(chptr, acptr, ":%s JOIN :%s", parv[1], parv[2]); */

      sendto_one(acptr,":%s NOTICE %s :Administrator %s has joined you to %s.", me.name, acptr->name, sptr->name, parv[2]);
      sendto_realops("ADMIN %s (%s@%s) used FJOIN to join %s (%s@%s) to %s.",
                    sptr->name,sptr->username,sptr->host,parv[1],acptr->username,acptr->host,parv[2]);

      Log("ADMIN %s (%s@%s) used FJOIN to join %s (%s@%s) to %s.",
                    sptr->name,sptr->username,sptr->host,parv[1],acptr->username,acptr->host,parv[2]);

      parv[1] = parv[2];
      (void)m_names(cptr, acptr, 2, parv);

    }

#endif /* FJOIN */

  return 0;
}

/*
** m_join
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = channel password (key)
*/
int     m_join(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  static char   jbuf[BUFSIZE];
  Link  *lp;
  struct Channel *chptr = NULL;
  char  *name, *key = NULL;
  int   i, flags = 0;
  char  *p = NULL, *p2 = NULL;
#ifdef ANTI_SPAMBOT
  int   successful_join_count = 0; /* Number of channels successfully joined */
#endif
  
  if (!(sptr->user))
    {
      /* something is *fucked* - bail */
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "JOIN");
      return 0;
    }

#ifdef NEED_SPLITCODE

  /* Check to see if the timer has timed out, and if so, see if
   * there are a decent number of servers now connected 
   * to consider any possible split over.
   * -Dianora
   */

  if (server_was_split)
    check_still_split();

#endif

  *jbuf = '\0';
  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, parv[1], ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      if (!check_channel_name(name))
        {
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                       me.name, parv[0], (unsigned char*) name);
          continue;
        }
      if (*name == '&' && !MyConnect(sptr))
        continue;
      if (*name == '0' && !atoi(name))
        *jbuf = '\0';
      else if (!IsChannelName(name))
        {
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                       me.name, parv[0], name);
          continue;
        }

#ifdef NO_JOIN_ON_SPLIT
      if (!IsEmpowered(sptr))
	{
	  if (server_was_split && MyClient(sptr) && (*name != '&'))
	    {
              sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                         me.name, parv[0], name);
              continue;
	    }
	}

#endif /* NO_JOIN_ON_SPLIT */

      if (*jbuf)
        (void)strcat(jbuf, ",");
      (void)strncat(jbuf, name, sizeof(jbuf) - i - 1);
      i += strlen(name)+1;
    }
  /*    (void)strcpy(parv[1], jbuf);*/

  p = NULL;
  if (parv[2])
    key = strtoken(&p2, parv[2], ",");
  parv[2] = NULL;       /* for m_names call later, parv[parc] must == NULL */
  for (name = strtoken(&p, jbuf, ","); name;
       key = (key) ? strtoken(&p2, NULL, ",") : NULL,
         name = strtoken(&p, NULL, ","))
    {
      /*
      ** JOIN 0 sends out a part for all channels a user
      ** has joined.
      */
      if (*name == '0' && !atoi(name))
        {
          if (sptr->user->channel == NULL)
            continue;
          while ((lp = sptr->user->channel))
            {
              chptr = lp->value.chptr;
              sendto_channel_butserv(chptr, sptr, PartFmt,
                                     parv[0], chptr->chname);
              remove_user_from_channel(sptr, chptr, 0);
            }

#ifdef ANTI_SPAMBOT       /* Dianora */

          if( MyConnect(sptr) && !IsEmpowered(sptr) )
            {
              if(SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
                {
                  sendto_ops_flags(FLAGS_BOTS,
                                     "User %s (%s@%s) is a possible spambot",
                                     sptr->name,
                                     sptr->username, sptr->host);
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
              else
                {
                  int t_delta;

                  if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
                      JOIN_LEAVE_COUNT_EXPIRE_TIME)
                    {
                      int decrement_count;
                      decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

                      if(decrement_count > sptr->join_leave_count)
                        sptr->join_leave_count = 0;
                      else
                        sptr->join_leave_count -= decrement_count;
                    }
                  else
                    {
                      if((CurrentTime - (sptr->last_join_time)) < SPAMTIME)
                        {
                          /* oh, its a possible spambot */
                          sptr->join_leave_count++;
                        }
                    }
                  sptr->last_leave_time = CurrentTime;
                }
            }
#endif
          sendto_match_servs(NULL, cptr, ":%s JOIN 0", parv[0]);
          continue;
        }
      
      if (MyConnect(sptr))
        {
          /*
          ** local client is first to enter previously nonexistent
          ** channel so make them (rightfully) the Channel
          ** Operator.
          */
           /*     flags = (ChannelExists(name)) ? 0 : CHFL_CHANOP; */

          /* To save a redundant hash table lookup later on */
           
           if((chptr = hash_find_channel(name, NullChn)))
             flags = 0;
           else
             {
#ifdef NO_CREATE_ON_SPLIT
               if (!IsEmpowered(sptr))
                 {
                   if (server_was_split && MyClient(sptr) && (*name != '&'))
                     {
                       sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                                  me.name, parv[0], name);
                       continue;
                     }
                 }
                 flags = CHFL_CHANOP;
#else
                 flags = CHFL_CHANOP;
#endif /* NO_CREATE_ON_SPLIT */
             }

           /* if its not a local channel, or isn't an oper
            * and server has been split
            */

          if ((sptr->user->joined >= MAXCHANNELSPERUSER) &&
             (!IsEmpowered(sptr) || (sptr->user->joined >= MAXCHANNELSPERUSER*3)))
            {
              sendto_one(sptr, form_str(ERR_TOOMANYCHANNELS),
                         me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
              if(successful_join_count)
                sptr->last_join_time = CurrentTime;
#endif
              return 0;
            }

#ifdef ANTI_SPAMBOT       /* Dianora */
          if(flags == 0)        /* if channel doesn't exist, don't penalize */
            successful_join_count++;
          if( SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
            { 
              /* Its already known as a possible spambot */
 
              if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
                sptr->oper_warn_count_down--;
              else
                sptr->oper_warn_count_down = 0;
 
              if(sptr->oper_warn_count_down == 0)
                {
                  sendto_ops_flags(FLAGS_BOTS,
                    "User %s (%s@%s) trying to join %s is a possible spambot",
                             sptr->name,
                             sptr->username,
                             sptr->host,
                             name);     
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
#ifndef ANTI_SPAMBOT_WARN_ONLY
              return 0; /* Don't actually JOIN anything, but don't let
                           spambot know that */
#endif
            }
#endif
        }
      else
        {
          /*
          ** complain for remote JOINs to existing channels
          ** (they should be SJOINs) -orabidoo
          */
          if (!ChannelExists(name))
            ts_warn("User on %s remotely JOINing new channel", 
                    sptr->user->server);
        }

      if(!chptr)        /* If I already have a chptr, no point doing this */
        chptr = get_channel(sptr, name, CREATE);

      if(chptr)
        {
          if (IsMember(sptr, chptr))    /* already a member, ignore this */
            continue;
        }
      else
        {
           sendto_one(sptr, form_str(ERR_UNAVAILRESOURCE),
                      me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /*
       * can_join checks for +i key, bans.
       * If a ban is found but an exception to the ban was found
       * flags will have CHFL_EXCEPTION set
       */

      if (MyConnect(sptr) && (i = can_join(sptr, chptr, key, &flags)))
        {
          sendto_one(sptr,
                    form_str(i), me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
          if(successful_join_count > 0)
            successful_join_count--;
#endif
          continue;
        }

      /*
      **  Complete user entry to the new channel (if any)
      */

      /* if (IsAdmin(sptr))
        {
         flags = CHFL_ADMIN;
        } */

      if (IsAdmin(sptr))
        {
         flags = CHFL_CHANOP;
        }

      add_user_to_channel(chptr, sptr, flags);

      /*
      **  Set timestamp if appropriate, and propagate
      */
      if (MyClient(sptr) && (flags & CHFL_CHANOP) && !IsAdmin(sptr))
        {
          chptr->channelts = CurrentTime;

          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s + :@%s", me.name,
                             chptr->channelts, name, parv[0]);
        }
      else if (MyClient(sptr))
        {
          sendto_match_servs(chptr, cptr,
                             ":%s SJOIN %lu %s + :%s", me.name,
                             chptr->channelts, name, parv[0]);
        }
      else
        sendto_match_servs(chptr, cptr, ":%s JOIN :%s", parv[0],
                           name);

      /*
      ** notify all other users on the new channel
      */

      sendto_channel_butserv(chptr, sptr, ":%s JOIN :%s", parv[0], name);

      if (MyClient(sptr))
        {
          if( flags & CHFL_CHANOP && !IsAdmin(sptr))
            {
              chptr->mode.mode |= MODE_TOPICLIMIT;
              chptr->mode.mode |= MODE_NOPRIVMSGS;

              sendto_channel_butserv(chptr, sptr,
                                 ":%s MODE %s +nt",
                                 me.name, chptr->chname);

              sendto_match_servs(chptr, sptr, 
                                 ":%s MODE %s +nt",
                                 me.name, chptr->chname);
            }

          del_invite(sptr, chptr);

          if (chptr->topic[0] != '\0')
            {
              sendto_one(sptr, form_str(RPL_TOPIC), me.name,
                         parv[0], name, chptr->topic);
#ifdef TOPIC_INFO
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], name,
                         chptr->topic_nick,
                         chptr->topic_time);
#endif
            }
          parv[1] = name;
          (void)m_names(cptr, sptr, 2, parv);
        }
    }

#ifdef ANTI_SPAMBOT
  if(MyConnect(sptr) && successful_join_count)
    sptr->last_join_time = CurrentTime;
#endif

 /* if (is_admin(sptr, chptr) && IsMember(sptr, chptr))
  {
   return 1;
  }
 else if (IsAdmin(sptr))
  {
   sendto_channel_butserv(chptr, sptr, ":%s MODE %s +ao %s",me.name,chptr->chname,sptr->name);
   sendto_match_servs(chptr, sptr, ":%s MODE %s +ao %s",me.name,chptr->chname,sptr->name);
  } */

 /* If client is an IRC Administrator, automatically set them +o on channel join -malign */
 /*if (is_chan_op(sptr, chptr) && IsMember(sptr,chptr))
  {
   return 1;
  }*/
 if (IsAdmin(sptr))
  {
   sendto_channel_butserv(chptr, sptr, ":%s MODE %s +o %s",me.name,chptr->chname,sptr->name);
   sendto_match_servs(chptr, sptr, ":%s MODE %s +o %s",me.name,chptr->chname,sptr->name);
  }

  return 0;
}

/*
** m_part
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int     m_part(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Channel *chptr;
  char  *p, *name;

  if (parc < 2 || parv[1][0] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "PART");
      return 0;
    }

  name = strtoken( &p, parv[1], ",");

#ifdef ANTI_SPAMBOT     /* Dianora */
      /* if its my client, and isn't an oper */

      if (name && MyConnect(sptr) && !IsEmpowered(sptr))
        {
          if(SPAMNUM && (sptr->join_leave_count >= SPAMNUM))
            {
              sendto_ops_flags(FLAGS_BOTS,
                               "User %s (%s@%s) is a possible spambot",
                               sptr->name,
                               sptr->username, sptr->host);
              sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
            }
          else
            {
              int t_delta;

              if( (t_delta = (CurrentTime - sptr->last_leave_time)) >
                  JOIN_LEAVE_COUNT_EXPIRE_TIME)
                {
                  int decrement_count;
                  decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

                  if(decrement_count > sptr->join_leave_count)
                    sptr->join_leave_count = 0;
                  else
                    sptr->join_leave_count -= decrement_count;
                }
              else
                {
                  if( (CurrentTime - (sptr->last_join_time)) < SPAMTIME)
                    {
                      /* oh, its a possible spambot */
                      sptr->join_leave_count++;
                    }
                }
              sptr->last_leave_time = CurrentTime;
            }
        }
#endif

  while ( name )
    {
      chptr = get_channel(sptr, name, 0);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                     me.name, parv[0], name);
          name = strtoken(&p, (char *)NULL, ",");
          continue;
        }

      if (!IsMember(sptr, chptr))
        {
          sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                     me.name, parv[0], name);
          name = strtoken(&p, (char *)NULL, ",");
          continue;
        }
      /*
      **  Remove user from the old channel (if any)
      */
            
      sendto_match_servs(chptr, cptr, PartFmt, parv[0], name);
            
      sendto_channel_butserv(chptr, sptr, PartFmt, parv[0], name);
      remove_user_from_channel(sptr, chptr, 0);
      name = strtoken(&p, (char *)NULL, ",");
    }
  return 0;
}

/*
** m_kick
**      parv[0] = sender prefix
**      parv[1] = channel
**      parv[2] = client to kick
**      parv[3] = kick comment
*/
/*
 * I've removed the multi channel kick, and the multi user kick
 * though, there are still remnants left ie..
 * "name = strtoken(&p, parv[1], ",");" in a normal kick
 * it will just be "KICK #channel nick"
 * A strchr() is going to be faster than a strtoken(), so rewritten
 * to use a strchr()
 *
 * It appears the original code was supposed to support 
 * "kick #channel1,#channel2 nick1,nick2,nick3." For example, look at
 * the original code for m_topic(), where 
 * "topic #channel1,#channel2,#channel3... topic" was supported.
 *
 * -Dianora
 */
int     m_kick(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  struct Client *who;
  struct Channel *chptr;
  int   chasing = 0;
  char  *comment;
  char  *name;
  char  *p = (char *)NULL;
  char  *user;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "KICK");
      return 0;
    }
  if (IsServer(sptr))
    sendto_ops("KICK from %s for %s %s",
               parv[0], parv[1], parv[2]);
  comment = (BadPtr(parv[3])) ? parv[0] : parv[3];
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

  *buf = '\0';
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  chptr = get_channel(sptr, name, !CREATE);
  if (!chptr)
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
      return(0);
    }

  /* You either have chan op privs, or you don't -Dianora */
  /* orabidoo and I discussed this one for a while...
   * I hope he approves of this code, (he did) users can get quite confused...
   *    -Dianora
   */
  /* Ensure Administrators can kick without having channel operator status -malign */
  if (!IsServer(sptr) && !is_chan_op(sptr, chptr) && !IsAdmin(sptr)) 
    { 
      /* was a user, not a server, and user isn't seen as a chanop here */
      
      if(MyConnect(sptr))
        {
          /* user on _my_ server, with no chanops.. so go away */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      if(chptr->channelts == 0)
        {
          /* If its a TS 0 channel, do it the old way */
          
          sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                     me.name, parv[0], chptr->chname);
          return(0);
        }

      /* Its a user doing a kick, but is not showing as chanop locally
       * its also not a user ON -my- server, and the channel has a TS.
       * There are two cases we can get to this point then...
       *
       *     1) connect burst is happening, and for some reason a legit
       *        op has sent a KICK, but the SJOIN hasn't happened yet or 
       *        been seen. (who knows.. due to lag...)
       *
       *     2) The channel is desynced. That can STILL happen with TS
       *        
       *     Now, the old code roger wrote, would allow the KICK to 
       *     go through. Thats quite legit, but lets weird things like
       *     KICKS by users who appear not to be chanopped happen,
       *     or even neater, they appear not to be on the channel.
       *     This fits every definition of a desync, doesn't it? ;-)
       *     So I will allow the KICK, otherwise, things are MUCH worse.
       *     But I will warn it as a possible desync.
       *
       *     -Dianora
       */

      /*          sendto_one(sptr, form_str(ERR_DESYNC),
       *           me.name, parv[0], chptr->chname);
       */

      /*
       * After more discussion with orabidoo...
       *
       * The code was sound, however, what happens if we have +h (TS4)
       * and some servers don't understand it yet? 
       * we will be seeing servers with users who appear to have
       * no chanops at all, merrily kicking users....
       * -Dianora
       */
    }

  p = strchr(parv[2],',');
  if(p)
    *p = '\0';
  user = parv[2]; /* strtoken(&p2, parv[2], ","); */

  if (!(who = find_chasing(sptr, user, &chasing)))
    {
      return(0);
    }

  if (IsMember(who, chptr) && !IsAdmin(who)) /* || IsMember(who, chptr) && IsLocOp(who) && !IsEmpowered(who)) */ /* Later for LocOp code -malign */
    {
#ifdef HIDE_OPS
      sendto_channel_chanops_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", parv[0],
                             name, who->name, comment);
      sendto_channel_non_chanops_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", NETWORK_NAME,
                             name, who->name, comment);

#else
      sendto_channel_butserv(chptr, sptr,
                             ":%s KICK %s %s :%s", parv[0],
                             name, who->name, comment);
#endif
      sendto_match_servs(chptr, cptr,
                         ":%s KICK %s %s :%s",
                         parv[0], name,
                         who->name, comment);
      remove_user_from_channel(who, chptr, 1);
    }
  /* Do not allow kicking of Global IRC Operators -malign */
  else if(IsAdmin(who)) /* && !IsLocOp(who)) */
    {
     sendto_one(sptr, "Unable to kick IRC Operators.");
     sendto_realops("USER %s (%s@%s) attempted to kick OPER %s (%s@%s) from channel %s.",
                     sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
     /* Log attempts to kick Global IRC Operators from channels -malign */
     Log("USER %s (%s@%s) attempted to kick OPER %s (%s@%s) from channel %s.",sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
    }

  /* Must add Locops code below */
  /* Allow IRC Operators to kick other IRC Operators. -malign */
  /* else if(IsEmpowered(who) && IsEmpowered(sptr))
    {
           sendto_match_servs(chptr, cptr,
                         ":%s KICK %s %s :%s",
                         parv[0], name,
                         who->name, comment);
      remove_user_from_channel(who, chptr, 1);
     sendto_realops("OPER %s (%s@%s) kicked OPER %s (%s@%s) from channel %s.",
                     sptr->name,sptr->username,sptr->host,who->name,who->username,who->host,chptr->chname);
    } */
  else
    sendto_one(sptr, form_str(ERR_USERNOTINCHANNEL),
               me.name, parv[0], user, name);

  return (0);
}

int     count_channels(struct Client *sptr)
{
  struct Channel      *chptr;
  int   count = 0;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    count++;
  return (count);
}

/* m_knock
**    parv[0] = sender prefix
**    parv[1] = channel
**  The KNOCK command has the following syntax:
**   :<sender> KNOCK <channel>
**  If a user is not banned from the channel they can use the KNOCK
**  command to have the server NOTICE the channel operators notifying
**  they would like to join.  Helpful if the channel is invite-only, the
**  key is forgotten, or the channel is full (INVITE can bypass each one
**  of these conditions.  Concept by Dianora <db@db.net> and written by
**  <anonymous>
**
** Just some flood control added here, five minute delay between each
** KNOCK -Dianora
**/
int     m_knock(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  char  *p, *name;
#ifdef USE_KNOCK
  struct Channel      *chptr;
  int knock_local = 0;
  static time_t last_used=0;
#endif

  /* anti flooding code,
   * I did have this in parse.c with a table lookup
   * but I think this will be less inefficient doing it in each
   * function that absolutely needs it
   *
   * -Dianora
   */

  if(IsServer(sptr))
    return 0;

  if(MyClient(sptr))
#ifdef USE_KNOCK
    knock_local = 1;
#else
  {
    sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND),
               me.name, sptr->name, "knock");
    return 0;
  }
#endif

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS), me.name, parv[0],
                 "KNOCK");
      return 0;
    }

  /* We will cut at the first comma reached, however we will not *
   * process anything afterwards.                                */

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

#ifdef USE_KNOCK
  if (!IsChannelName(name) || !(chptr = hash_find_channel(name, NullChn)))
    {
      if(knock_local)    
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), 
	           me.name, parv[0], name);
		   
      return 0;
    }

  if(!((chptr->mode.mode & MODE_INVITEONLY) ||
       (*chptr->mode.key) ||
       (chptr->mode.limit && chptr->users >= chptr->mode.limit )
       ))
    {
      if(knock_local)
        sendto_one(sptr,":%s NOTICE %s :*** Notice -- Channel is open!",
                   me.name, sptr->name);
      return 0;
    }

  /* don't allow a knock if the user is banned, or the channel is secret */
  if (knock_local && ((chptr->mode.mode & MODE_SECRET) || 
      (is_banned(sptr, chptr) == CHFL_BAN)) )
    {
      sendto_one(sptr, form_str(ERR_CANNOTSENDTOCHAN), me.name, parv[0],
                 name);
      return 0;
    }

  /* if the user is already on channel, then a knock is pointless! */
  if (IsMember(sptr, chptr))
    {
      if(knock_local)
        sendto_one(sptr,":%s NOTICE %s :*** Notice -- You are on channel already!",
                   me.name,
                   sptr->name);
      return 0;
    }

  /* flood control server wide, clients on KNOCK
   * opers are not flood controlled.
   */
  if(knock_local)
  {
    if(!IsEmpowered(sptr))
    {
      if((last_used + PACE_WAIT) > CurrentTime)
        return 0;
      else
        last_used = CurrentTime;
    }

    /* flood control individual clients on KNOCK
     * the ugly possibility still exists, 400 clones could all KNOCK
     * on a channel at once, flooding all the ops. *ugh*
     * Remember when life was simpler?
     * -Dianora
     */
  
    if((sptr->last_knock + KNOCK_DELAY) > CurrentTime)
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Wait %d seconds before another knock",
                 me.name,
                 sptr->name,
                 KNOCK_DELAY - (CurrentTime - sptr->last_knock));
      return 0;
    }

    if((chptr->last_knock + KNOCK_DELAY_CHANNEL) > CurrentTime)
    {
      sendto_one(sptr, ":%s NOTICE %s :*** Notice -- Wait %d seconds before another knock to %s",
                 me.name, sptr->name,
		 KNOCK_DELAY_CHANNEL - (CurrentTime - chptr->last_knock),
		 name);
      return 0;
    }

    sptr->last_knock = CurrentTime;

    sendto_one(sptr,":%s NOTICE %s :*** Notice -- Your KNOCK has been delivered",
                 me.name,
                 sptr->name);
  }
  
  chptr->last_knock = CurrentTime;
  
  /* using &me and me.name won't deliver to clients not on this server
   * so, the notice will have to appear from the "knocker" ick.
   *
   * Ideally, KNOCK would be routable. Also it would be nice to add
   * a new channel mode. Perhaps some day.
   * For now, clients that don't want to see KNOCK requests will have
   * to use client side filtering. 
   *
   * -Dianora
   */

  {
    char message[NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30];

    /* bit of paranoid, be a shame if it cored for this -Dianora */
    if(sptr->user)
    {
      ircsprintf(message,"KNOCK: %s (%s [%s@%s] has asked for an invite)",
                   chptr->chname,
                   sptr->name,
                   sptr->username,
                   sptr->host);
      send_knock(sptr, cptr, chptr, MODE_CHANOP, message,
                 (parc > 3) ? parv[3] : "");
    }
  }

#endif

  /* send_knock is called if we have knock enabled.. if we dont, forward the
   * knock anyway. --fl_
   */
#ifndef USE_KNOCK
  sendto_match_cap_servs(NULL, cptr, CAP_KNOCK,
                         ":%s KNOCK %s %s",
			  sptr->name, name, (parc > 3) ? parv[3] : "");
#endif
  
  return 0;
}

/*
** m_topic
**      parv[0] = sender prefix
**      parv[1] = topic text
*/
int     m_topic(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr = NullChn;
  char  *topic = (char *)NULL, *name, *p = (char *)NULL;
  
  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "TOPIC");
      return 0;
    }

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* multi channel topic's are now known to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  if (name && IsChannelName(name))
    {
      chptr = hash_find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (!IsMember(sptr, chptr))
        {
          sendto_one(sptr, form_str(ERR_NOTONCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (parc > 2) /* setting topic */
        topic = parv[2];

      if(topic) /* a little extra paranoia never hurt */
        {
          if ((chptr->mode.mode & MODE_TOPICLIMIT) == 0 ||
               is_chan_op(sptr, chptr) || IsEmpowered(sptr))
            {
              /* setting a topic */
              /*
               * chptr zeroed
               */
              strncpy_irc(chptr->topic, topic, TOPICLEN);
#ifdef TOPIC_INFO
              /*
               * XXX - this truncates the topic_nick if
               * strlen(sptr->name) > NICKLEN
               */
              strncpy_irc(chptr->topic_nick, sptr->name, NICKLEN);
              chptr->topic_time = CurrentTime;
#endif
              sendto_match_servs(chptr, cptr,":%s TOPIC %s :%s",
                                 parv[0], chptr->chname,
                                 chptr->topic);
              sendto_channel_butserv(chptr, sptr, ":%s TOPIC %s :%s",
                                     parv[0],
                                     chptr->chname, chptr->topic);
            }
          else
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], chptr->chname);
        }
      else  /* only asking  for topic  */
        {
          if (chptr->topic[0] == '\0')
            sendto_one(sptr, form_str(RPL_NOTOPIC),
                       me.name, parv[0], chptr->chname);
          else
            {
              sendto_one(sptr, form_str(RPL_TOPIC),
                         me.name, parv[0],
                         chptr->chname, chptr->topic);
#ifdef TOPIC_INFO
              sendto_one(sptr, form_str(RPL_TOPICWHOTIME),
                         me.name, parv[0], chptr->chname,
                         chptr->topic_nick,
                         chptr->topic_time);
#endif
            }
        }
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                 me.name, parv[0], name);
    }

  return 0;
}

/*
** m_invite
**      parv[0] - sender prefix
**      parv[1] - user to invite
**      parv[2] - channel number
*/
int     m_invite(struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{
  struct Client *acptr;
  struct Channel *chptr;
  int need_invite=NO;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "INVITE");
      return -1;
    }

#ifdef SERVERHIDE
  if (!IsEmpowered(sptr) && parv[2][0] == '&') {
    sendto_one(sptr, ":%s NOTICE %s :INVITE to local channels is disabled in this server",
               me.name, parv[0]);
    return 0;
  }
#endif

  /* A little sanity test here */
  if(!sptr->user)
    return 0;

  if (!(acptr = find_person(parv[1], (struct Client *)NULL)))
    {
      sendto_one(sptr, form_str(ERR_NOSUCHNICK),
                 me.name, parv[0], parv[1]);
      return 0;
    }

  if (!check_channel_name(parv[2]))
    { 
      sendto_one(sptr, form_str(ERR_BADCHANNAME),
                 me.name, parv[0], (unsigned char *)parv[2]);
      return 0;
    }

  if (!IsChannelName(parv[2]))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  /* Do not send local channel invites to users if they are not on the
   * same server as the person sending the INVITE message. 
   */
  /* Possibly should be an error sent to sptr */
  if (!MyConnect(acptr) && (parv[2][0] == '&'))
    return 0;

  if (!(chptr = hash_find_channel(parv[2], NullChn)))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOSUCHCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  /* By this point, chptr is non NULL */  

  if (!IsMember(sptr, chptr))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NOTONCHANNEL),
                   me.name, parv[0], parv[2]);
      return 0;
    }

  if (IsMember(acptr, chptr))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_USERONCHANNEL),
                   me.name, parv[0], parv[1], parv[2]);
      return 0;
    }

  if (chptr && (chptr->mode.mode & MODE_INVITEONLY))
    {
      need_invite = YES;

      if (!is_chan_op(sptr, chptr))
        {
          if (MyClient(sptr))
            sendto_one(sptr, form_str(ERR_CHANOPRIVSNEEDED),
                       me.name, parv[0], parv[2]);
          return -1;
        }
    }

  /*
   * due to some whining I've taken out the need for the channel
   * being +i before sending an INVITE. It was intentionally done this
   * way, it makes no sense (to me at least) letting the server send
   * an unnecessary invite when a channel isn't +i !
   * bah. I can't be bothered arguing it
   * -Dianora
   */
  if (MyConnect(sptr) /* && need_invite*/ )
    {
      sendto_one(sptr, form_str(RPL_INVITING), me.name, parv[0],
                 acptr->name, ((chptr) ? (chptr->chname) : parv[2]));
      if (acptr->user->away)
        sendto_one(sptr, form_str(RPL_AWAY), me.name, parv[0],
                   acptr->name, acptr->user->away);
      
      if( need_invite )
        {
          /* Send a NOTICE to all channel operators concerning chanops who  *
           * INVITE other users to the channel when it is invite-only (+i). *
           * The NOTICE is sent from the local server.                      */

          /* Only allow this invite notice if the channel is +p
           * i.e. "paranoid"
           * -Dianora
           */

          if (chptr && (chptr->mode.mode & MODE_PRIVATE))
            { 
              char message[NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30];

              /* bit of paranoia, be a shame if it cored for this -Dianora */
              if(acptr->user)
                {
                  ircsprintf(message,
                             "INVITE: %s (%s invited %s [%s@%s])",
                             chptr->chname,
                             sptr->name,
                             acptr->name,
                             acptr->username,
                             acptr->host);

                  sendto_channel_type(cptr, sptr, chptr,
                                      MODE_CHANOP,
                                      chptr->chname,
                                      "PRIVMSG",
                                      message);
                }
            }
        }
    }

  if(MyConnect(acptr) && need_invite)
    add_invite(acptr, chptr);

  sendto_prefix_one(acptr, sptr, ":%s INVITE %s :%s",
                    parv[0], acptr->name, parv[2]);
  return 0;
}


/************************************************************************
 * m_names() - Added by Jto 27 Apr 1989
 ************************************************************************/

/*
** m_names
**      parv[0] = sender prefix
**      parv[1] = channel
*/
/*
 * Modified to report possible names abuse
 * drastically modified to not show all names, just names
 * on given channel names.
 *
 * -Dianora
 */
/* maximum names para to show to opers when abuse occurs */
#define TRUNCATED_NAMES 20

int     m_names( struct Client *cptr,
                 struct Client *sptr,
                 int parc,
                 char *parv[])
{ 
  struct Channel *chptr;
  struct Client *c2ptr;
  Link  *lp;
  struct Channel *ch2ptr = NULL;
  int   idx, flag = 0, len, mlen;
  char  *s, *para = parc > 1 ? parv[1] : NULL;
  int comma_count=0;
  int char_count=0;

  /* And throw away non local names requests that do get here -Dianora */
  if(!MyConnect(sptr))
    return 0;

  mlen = strlen(me.name) + NICKLEN + 7;

  if (!BadPtr(para))
    {
      /* Here is the lamer detection code
       * P.S. meta, GROW UP
       * -Dianora 
       */
      for(s = para; *s; s++)
        {
          char_count++;
          if(*s == ',')
            comma_count++;
          if(comma_count > 1)
            {
              if(char_count > TRUNCATED_NAMES)
                para[TRUNCATED_NAMES] = '\0';
              else
                {
                  s++;
                  *s = '\0';
                }
              sendto_one(sptr, form_str(ERR_TOOMANYTARGETS),
                         me.name, sptr->name, "NAMES",1);
              return 0;
            }
        }

      if ((s = strchr(para, ',')) != NULL)
        *s = '\0';
      if (!check_channel_name(para))
        { 
          sendto_one(sptr, form_str(ERR_BADCHANNAME),
                     me.name, parv[0], (unsigned char *)para);
          return 0;
        }

      ch2ptr = hash_find_channel(para, NULL);
    }

  *buf = '\0';
  
  /* 
   *
   * First, do all visible channels (public and the one user self is)
   */

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      if ((chptr != ch2ptr) && !BadPtr(para))
        continue; /* -- wanted a specific channel */
      if (!MyConnect(sptr) && BadPtr(para))
        continue;
      if (!ShowChannel(sptr, chptr))
        continue; /* -- users on this are not listed */
      
      /* Find users on same channel (defined by chptr) */

      (void)strcpy(buf, "* ");
      len = strlen(chptr->chname);
      (void)strcpy(buf + 2, chptr->chname);
      (void)strcpy(buf + 2 + len, " :");

      if (PubChannel(chptr))
        *buf = '=';
      else if (SecretChannel(chptr))
        *buf = '@';
      idx = len + 4;
      flag = 1;
      for (lp = chptr->members; lp; lp = lp->next)
        {
          c2ptr = lp->value.cptr;
          if (IsInvisible(c2ptr) && !IsMember(sptr,chptr))
            continue;
#ifdef HIDE_OPS
	  if(is_chan_op(sptr,chptr))
#endif
	    {
              /* if (lp->flags & CHFL_ADMIN)
                {
                   strcat(buf, "*");
                   idx++;
                } */
              if (lp->flags & CHFL_CHANOP)
		{
		   strcat(buf, "@");
		   idx++;
		}
	      else if (lp->flags & CHFL_VOICE)
		{
		  strcat(buf, "+");
		  idx++;
		}
	    }
          strncat(buf, c2ptr->name, NICKLEN);
          idx += strlen(c2ptr->name) + 1;
          flag = 1;
          strcat(buf," ");
          if (mlen + idx + NICKLEN > BUFSIZE - 3)
            {
              sendto_one(sptr, form_str(RPL_NAMREPLY),
                         me.name, parv[0], buf);
              strncpy_irc(buf, "* ", 3);
              strncpy_irc(buf + 2, chptr->chname, len + 1);
              strcat(buf, " :");
              if (PubChannel(chptr))
                *buf = '=';
              else if (SecretChannel(chptr))
                *buf = '@';
              idx = len + 4;
              flag = 0;
            }
        }
      if (flag)
        sendto_one(sptr, form_str(RPL_NAMREPLY),
                   me.name, parv[0], buf);
    }
  if (!BadPtr(para))
    {
      sendto_one(sptr, form_str(RPL_ENDOFNAMES), me.name, parv[0],
                 para);
      return(1);
    }

  /* Second, do all non-public, non-secret channels in one big sweep */

  strncpy_irc(buf, "* * :", 6);
  idx = 5;
  flag = 0;
  for (c2ptr = GlobalClientList; c2ptr; c2ptr = c2ptr->next)
    {
      struct Channel *ch3ptr;
      int       showflag = 0, secret = 0;

      if (!IsPerson(c2ptr) || IsInvisible(c2ptr))
        continue;
      lp = c2ptr->user->channel;
      /*
       * dont show a client if they are on a secret channel or
       * they are on a channel sptr is on since they have already
       * been show earlier. -avalon
       */
      while (lp)
        {
          ch3ptr = lp->value.chptr;
          if (PubChannel(ch3ptr) || IsMember(sptr, ch3ptr))
            showflag = 1;
          if (SecretChannel(ch3ptr))
            secret = 1;
          lp = lp->next;
        }
      if (showflag) /* have we already shown them ? */
        continue;
      if (secret) /* on any secret channels ? */
        continue;
      (void)strncat(buf, c2ptr->name, NICKLEN);
      idx += strlen(c2ptr->name) + 1;
      (void)strcat(buf," ");
      flag = 1;
      if (mlen + idx + NICKLEN > BUFSIZE - 3)
        {
          sendto_one(sptr, form_str(RPL_NAMREPLY),
                     me.name, parv[0], buf);
          strncpy_irc(buf, "* * :", 6);
          idx = 5;
          flag = 0;
        }
    }

  if (flag)
    sendto_one(sptr, form_str(RPL_NAMREPLY), me.name, parv[0], buf);

  sendto_one(sptr, form_str(RPL_ENDOFNAMES), me.name, parv[0], "*");
  return(1);
}


void    send_user_joins(struct Client *cptr, struct Client *user)
{
  Link  *lp;
  struct Channel *chptr;
  int   cnt = 0, len = 0, clen;
  char   *mask;

  *buf = ':';
  (void)strcpy(buf+1, user->name);
  (void)strcat(buf, " JOIN ");
  len = strlen(user->name) + 7;

  for (lp = user->user->channel; lp; lp = lp->next)
    {
      chptr = lp->value.chptr;
      if (*chptr->chname == '&')
        continue;
      if ((mask = strchr(chptr->chname, ':')))
        if (!match(++mask, cptr->name))
          continue;
      clen = strlen(chptr->chname);
      if (clen > (size_t) BUFSIZE - 7 - len)
        {
          if (cnt)
            sendto_one(cptr, "%s", buf);
          *buf = ':';
          (void)strcpy(buf+1, user->name);
          (void)strcat(buf, " JOIN ");
          len = strlen(user->name) + 7;
          cnt = 0;
        }
      (void)strcpy(buf + len, chptr->chname);
      cnt++;
      len += clen;
      if (lp->next)
        {
          len++;
          (void)strcat(buf, ",");
        }
    }
  if (*buf && cnt)
    sendto_one(cptr, "%s", buf);

  return;
}

static  void sjoin_sendit(struct Client *cptr,
                          struct Client *sptr,
                          struct Channel *chptr,
                          char *from)
{
  sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", from,
                         chptr->chname, modebuf, parabuf);
}

/*
 * m_sjoin
 * parv[0] - sender
 * parv[1] - TS
 * parv[2] - channel
 * parv[3] - modes + n arguments (key and/or limit)
 * parv[4+n] - flags+nick list (all in one parameter)

 * 
 * process a SJOIN, taking the TS's into account to either ignore the
 * incoming modes or undo the existing ones or merge them, and JOIN
 * all the specified users while sending JOIN/MODEs to non-TS servers
 * and to clients
 */


int     m_sjoin(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Channel *chptr;
  struct Client       *acptr;
  time_t        newts;
  time_t        oldts;
  time_t        tstosend;
  static        Mode mode, *oldmode;
  Link  *l;
  int   args = 0, haveops = 0, keep_our_modes = 1, keep_new_modes = 1;
  int   doesop = 0, what = 0, pargs = 0, fl, people = 0, isnew;
  /* loop unrolled this is now redundant */
  /*  int ip; */
  char *s, *s0;
  static        char numeric[16], sjbuf[BUFSIZE];
  char  *mbuf = modebuf, *t = sjbuf, *p;

  /* wipe sjbuf so we don't use old nicks if we get an empty SJOIN */
  *sjbuf = '\0';

  if (IsClient(sptr) || parc < 5)
    return 0;
  if (!IsChannelName(parv[2]))
    return 0;

  if (!check_channel_name(parv[2]))
     { 
       return 0;
     }

  /* comstud server did this, SJOIN's for
   * local channels can't happen.
   */

  if(*parv[2] == '&')
    return 0;

  newts = atol(parv[1]);
  memset(&mode, 0, sizeof(mode));

  s = parv[3];
  while (*s)
    switch(*(s++))
      {
      case 'i':
        mode.mode |= MODE_INVITEONLY;
        break;
      case 'n':
        mode.mode |= MODE_NOPRIVMSGS;
        break;
      case 'p':
        mode.mode |= MODE_PRIVATE;
        break;
      case 's':
        mode.mode |= MODE_SECRET;
        break;
#ifdef SERVICES
      case 'r':
        mode.mode |= MODE_REGISTERED;
        break;
#endif /* SERVICES */
      case 'm':
        mode.mode |= MODE_MODERATED;
        break;
      case 't':
        mode.mode |= MODE_TOPICLIMIT;
        break;
      case 'k':
        strncpy_irc(mode.key, parv[4 + args], KEYLEN);
        args++;
        if (parc < 5+args) return 0;
        break;
      case 'l':
        mode.limit = atoi(parv[4+args]);
        args++;
        if (parc < 5+args) return 0;
        break;
      }

  *parabuf = '\0';

  isnew = ChannelExists(parv[2]) ? 0 : 1;
  chptr = get_channel(sptr, parv[2], CREATE);

  oldts = chptr->channelts;

  /*
   * If an SJOIN ever happens on a channel, assume the split is over
   * for this channel. best I think we can do for now -Dianora
   */

  /* If the TS goes to 0 for whatever reason, flag it
   * ya, I know its an invasion of privacy for those channels that
   * want to keep TS 0 *shrug* sorry
   */

  if(!isnew && !newts && oldts)
    {
      sendto_channel_butserv(chptr, &me,
             ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to 0",
              me.name, chptr->chname, chptr->chname, oldts);
      sendto_realops("Server %s changing TS on %s from %lu to 0",
                     sptr->name,parv[2],oldts);
    }

  doesop = (parv[4+args][0] == '@' || parv[4+args][1] == '@');

  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
        haveops++;
        break;
      }

  oldmode = &chptr->mode;

#ifdef IGNORE_BOGUS_TS
  if (newts < 800000000)
    {
      sendto_realops("*** Bogus TS %lu on %s ignored from %s",
		     (unsigned long) newts,
		     chptr->chname,
		     cptr->name);
      newts = oldts;
    }
#endif
  
  if (isnew)
    chptr->channelts = tstosend = newts;
  else if (newts == 0 || oldts == 0)
    chptr->channelts = tstosend = 0;
  else if (newts == oldts)
    tstosend = oldts;
  /* if their TS is older, remove our modes, change the channel ts to their
   * ts and accept their modes
   */
  else if (newts < oldts)
  {
  /* new behaviour, never keep our modes */
#ifdef TS5
    keep_our_modes = NO;
    chptr->channelts = tstosend = newts;
    
/* old behaviour, keep our modes if their sjoin is opless */
#else
    if(doesop)
      keep_our_modes = NO;
    if(haveops && !doesop)
    {
      tstosend = oldts;
      total_ignoreops++;
    }
    else
      chptr->channelts = tstosend = newts;
#endif /* TS5 */

  }
  else
  {

    /* if the channel is juped locally, and has no users, we need to use
     * their ts, whether its older or not, else we'll create a ts desync
     * when we dont even have users.. --fl_
     */
#ifdef JUPE_CHANNEL
    if(chptr->juped && chptr->users == 0)
      chptr->channelts = tstosend = newts;
    else
    {
#endif
#ifdef TS5
      keep_new_modes = NO;
      tstosend = oldts;
#else
      if(haveops)
        keep_new_modes = NO;
      if (doesop && !haveops)
      {
	total_hackops++;
        chptr->channelts = tstosend = newts;
      }
      else
        tstosend = oldts;
#endif /* TS5 */	
#ifdef JUPE_CHANNEL
    }
#endif    
  }

  if (!keep_new_modes)
    mode = *oldmode;
  else if (keep_our_modes)
    {
      mode.mode |= oldmode->mode;
      if (oldmode->limit > mode.limit)
        mode.limit = oldmode->limit;
      if (strcmp(mode.key, oldmode->key) < 0)
        strcpy(mode.key, oldmode->key);
    }

  /* This loop unrolled below for speed
   */
  /*
  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & mode.mode) && !(flags[ip].mode & oldmode->mode))
      {
        if (what != 1)
          {
            *mbuf++ = '+';
            what = 1;
          }
        *mbuf++ = flags[ip].letter;
      }
      */

  if((MODE_PRIVATE    & mode.mode) && !(MODE_PRIVATE    & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'p';
    }
  if((MODE_SECRET     & mode.mode) && !(MODE_SECRET     & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 's';
    }
#ifdef SERVICES
  if((MODE_REGISTERED  & mode.mode) && !(MODE_REGISTERED  & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'r';
    }
#endif /* SERVICES */
  if((MODE_MODERATED  & mode.mode) && !(MODE_MODERATED  & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & mode.mode) && !(MODE_NOPRIVMSGS & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'n';
    }
  if((MODE_TOPICLIMIT & mode.mode) && !(MODE_TOPICLIMIT & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 't';
    }
  if((MODE_INVITEONLY & mode.mode) && !(MODE_INVITEONLY & oldmode->mode))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;             /* This one is actually redundant now */
        }
      *mbuf++ = 'i';
    }

  /* This loop unrolled below for speed
   */
  /*
  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & oldmode->mode) && !(flags[ip].mode & mode.mode))
      {
        if (what != -1)
          {
            *mbuf++ = '-';
            what = -1;
          }
        *mbuf++ = flags[ip].letter;
      }
      */
  if((MODE_PRIVATE    & oldmode->mode) && !(MODE_PRIVATE    & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'p';
    }
  if((MODE_SECRET     & oldmode->mode) && !(MODE_SECRET     & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 's';
    }
#ifdef SERVICES
  if((MODE_REGISTERED  & oldmode->mode) && !(MODE_REGISTERED  & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'r';
    }
#endif /* SERVICES */
  if((MODE_MODERATED  & oldmode->mode) && !(MODE_MODERATED  & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'm';
    }
  if((MODE_NOPRIVMSGS & oldmode->mode) && !(MODE_NOPRIVMSGS & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'n';
    }
  if((MODE_TOPICLIMIT & oldmode->mode) && !(MODE_TOPICLIMIT & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 't';
    }
  if((MODE_INVITEONLY & oldmode->mode) && !(MODE_INVITEONLY & mode.mode))
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'i';
    }

  if (oldmode->limit && !mode.limit)
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'l';
    }
  if (oldmode->key[0] && !mode.key[0])
    {
      if (what != -1)
        {
          *mbuf++ = '-';
          what = -1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, oldmode->key);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.limit && oldmode->limit != mode.limit)
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'l';
      ircsprintf(numeric, "%d", mode.limit);
      if ((s = strchr(numeric, ' ')))
        *s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.key[0] && strcmp(oldmode->key, mode.key))
    {
      if (what != 1)
        {
          *mbuf++ = '+';
          what = 1;
        }
      *mbuf++ = 'k';
      strcat(parabuf, mode.key);
      strcat(parabuf, " ");
      pargs++;
    }
  
  chptr->mode = mode;

  if (!keep_our_modes)
    {
      what = 0;
      for (l = chptr->members; l && l->value.cptr; l = l->next)
        {

          /* if (l->flags & MODE_ADMIN)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'a';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
#ifdef SERVERHIDE
                  sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
                  sjoin_sendit(cptr, sptr, chptr,
                               parv[0]);
#endif
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_ADMIN;
            } */

          if (l->flags & MODE_CHANOP)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'o';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
#ifdef SERVERHIDE
                  sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
                  sjoin_sendit(cptr, sptr, chptr,
                               parv[0]);
#endif
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_CHANOP;
            }
          if (l->flags & MODE_VOICE)
            {
              if (what != -1)
                {
                  *mbuf++ = '-';
                  what = -1;
                }
              *mbuf++ = 'v';
              strcat(parabuf, l->value.cptr->name);
              strcat(parabuf, " ");
              pargs++;
              if (pargs >= MAXMODEPARAMS)
                {
                  *mbuf = '\0';
#ifdef SERVERHIDE
                  sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
                  sjoin_sendit(cptr, sptr, chptr,
                               parv[0]);
#endif
                  mbuf = modebuf;
                  *mbuf = parabuf[0] = '\0';
                  pargs = what = 0;
                }
              l->flags &= ~MODE_VOICE;
            }
        }
        sendto_channel_butserv(chptr, &me,
            ":%s NOTICE %s :*** Notice -- TS for %s changed from %lu to %lu",
            me.name, chptr->chname, chptr->chname, oldts, newts);
    }
  if (mbuf != modebuf)
    {
      *mbuf = '\0';
#ifdef SERVERHIDE
      sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
      sjoin_sendit(cptr, sptr, chptr, parv[0]);
#endif
    }

  *modebuf = *parabuf = '\0';
  if (parv[3][0] != '0' && keep_new_modes)
    channel_modes(sptr, modebuf, parabuf, chptr);
  else
    {
      modebuf[0] = '0';
      modebuf[1] = '\0';
    }

  ircsprintf(t, ":%s SJOIN %lu %s %s %s :", parv[0], tstosend, parv[2],
          modebuf, parabuf);
  t += strlen(t);

  mbuf = modebuf;
  parabuf[0] = '\0';
  pargs = 0;
  *mbuf++ = '+';

  for (s = s0 = strtoken(&p, parv[args+4], " "); s;
       s = s0 = strtoken(&p, (char *)NULL, " "))
    {
      fl = 0;
      /* if (*s == '*' || s[1] == '*')
        fl |= MODE_ADMIN; */
      if (*s == '@' || s[1] == '@')
        fl |= MODE_CHANOP;
      if (*s == '+' || s[1] == '+')
        fl |= MODE_VOICE;
      if (!keep_new_modes)
       {
        if (fl & MODE_CHANOP)
          {
            fl = MODE_DEOPPED;
          }
        else
          {
            fl = 0;
          }
       }
      /* while (*s == '*' || *s == '@' || *s == '+') */
      while (*s == '@' || *s == '+')
        s++;
      if (!(acptr = find_chasing(sptr, s, NULL)))
        continue;
      if (acptr->from != cptr)
        continue;
      people++;
      if (!IsMember(acptr, chptr))
        {
          add_user_to_channel(chptr, acptr, fl);
          sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s",
                                 s, parv[2]);
        }
      if (keep_new_modes)
        strcpy(t, s0);
      else
        strcpy(t, s);
      t += strlen(t);
      *t++ = ' ';

      /* if (fl & MODE_ADMIN)
        {
          *mbuf++ = 'a';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
#ifdef SERVERHIDE
              sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
              sjoin_sendit(cptr, sptr, chptr, parv[0]);
#endif
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        } */

      if (fl & MODE_CHANOP)
        {
          *mbuf++ = 'o';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
#ifdef SERVERHIDE
              sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
              sjoin_sendit(cptr, sptr, chptr, parv[0]);
#endif
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
      if (fl & MODE_VOICE)
        {
          *mbuf++ = 'v';
          strcat(parabuf, s);
          strcat(parabuf, " ");
          pargs++;
          if (pargs >= MAXMODEPARAMS)
            {
              *mbuf = '\0';
#ifdef SERVERHIDE
              sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
              sjoin_sendit(cptr, sptr, chptr, parv[0]);
#endif
              mbuf = modebuf;
              *mbuf++ = '+';
              parabuf[0] = '\0';
              pargs = 0;
            }
        }
    }
  
  *mbuf = '\0';
  if (pargs)
#ifdef SERVERHIDE
    sjoin_sendit(&me,  sptr, chptr, parv[0]);
#else
    sjoin_sendit(cptr, sptr, chptr, parv[0]);
#endif
  if (people)
    {
      if (t[-1] == ' ')
        t[-1] = '\0';
      else
        *t = '\0';
      sendto_match_servs(chptr, cptr, "%s", sjbuf);
    }
  return 0;
}


#ifdef JUPE_CHANNEL

/*
 * report_juped_channels
 *
 * inputs	- pointer to client to report to
 * output	- none
 * side effects	- walks hash tables directly, reporting all juped channels
 */

/* I used a random available numeric for this stats... Any servers 
 * using a specific one? If so, tell me and I'll make a proper
 * RPL_STATSJLINE def.
 * --einride
 * made RPL_STATSQLINE like hybrid-7 - Dianora
 */
void report_juped_channels(struct Client *sptr)
{
  struct Channel * chptr;
  int i;

  if (sptr->user == NULL)
    return;

  for (i=0;i<CH_MAX;i++)
  {
    for (chptr = (struct Channel *) (hash_get_channel_block(i).list);
	 chptr != NULL; chptr = chptr->hnextch)
    {
      if (chptr->juped)
      {
	sendto_one(sptr, form_str(RPL_STATSQLINE),
		   me.name,
		   sptr->name,
		   'q',
		   chptr->chname,
		   "","","oper juped");
      }
    }
  }
}
#endif
