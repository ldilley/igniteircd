/*
 * dline_conf.c
 *
 * $Id: dline_conf.c,v 1.1.1.1 2006/03/08 23:28:08 malign Exp $
 */
#include "dline_conf.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "send.h"
#include "struct.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>


typedef struct ip_subtree
{
  unsigned long ip;
  unsigned long ip_mask;
  aConfItem *conf;
  struct ip_subtree *left;    
  struct ip_subtree *right;
} IP_SUBTREE;

/* the D/d-line structure */
struct ip_subtree *Dline[256];   
/* the ip K-line structure, tracks E/I/K */
struct ip_subtree *ip_Kline[256];   

/* the oracle thingy */
unsigned long oracle[256];
/* the oracle thingy */
unsigned long ike_oracle[256];

/* leftover D/d-lines */
aConfItem *leftover = NULL;

/* defined in mtrie_conf.c */
extern char *show_iline_prefix(aClient *,aConfItem *,char *);

/* do these belong here? */
void walk_the_dlines(aClient *, struct ip_subtree *);
void walk_the_ip_Klines(aClient *,struct ip_subtree *, char, int);
struct ip_subtree *new_ip_subtree(unsigned long,
                                  unsigned long,
                                  aConfItem *,
                                  struct ip_subtree *,
                                  struct ip_subtree *);
struct ip_subtree *insert_ip_subtree(struct ip_subtree *, 
                                     struct ip_subtree *);
struct ip_subtree *find_ip_subtree(struct ip_subtree *,
                                   unsigned long );
struct ip_subtree *delete_ip_subtree(struct ip_subtree *,
                                     unsigned long,
                                     unsigned long,
                                     aConfItem **);
aConfItem *trim_Dlines(aConfItem *);
aConfItem *trim_ip_Klines(aConfItem *, int);
aConfItem *trim_ip_Elines(aConfItem *, int);
struct ip_subtree *destroy_ip_subtree(struct ip_subtree *);
aConfItem *find_exception(unsigned long);
aConfItem *rescan_dlines(aConfItem *);



/*
 * new_ip_subtree - just makes a new node 
 */
struct ip_subtree *new_ip_subtree(unsigned long ip, 
                                  unsigned long mask,
                                  aConfItem *clist,
                                  struct ip_subtree *left,
                                  struct ip_subtree *right)
{
  struct ip_subtree *temp;
  temp=(struct ip_subtree*)MyMalloc(sizeof(struct ip_subtree));
  temp->ip=ip & mask;             /* enforce masking here to save time later */
  temp->ip_mask=mask;
  temp->conf=clist;
  temp->left=left;
  temp->right=right;
  return temp;
}

/*
 * insert_ip_subtree - insert a node or tree into the specified tree.
 * Make sure that collisions have been removed ahead of time.
 * (Dline[xxx])
 * -good
 */
struct ip_subtree *insert_ip_subtree(struct ip_subtree *head, 
                                     struct ip_subtree *node)
{
  if (!head) return node;   /* null, insert here */
  if (head->ip > node->ip)
    {
      head->left=insert_ip_subtree(head->left, node);
    }
  else
    {
      head->right=insert_ip_subtree(head->right, node);
    }
  return head;
}

/*
 * find_ip_subtree - finds the node that matches a given IP in the specified
 * ip_subtree (Dline[xxx]).
 * -good
 */
struct ip_subtree *find_ip_subtree(struct ip_subtree *head,
                                   unsigned long ip)
{
  unsigned long mask;
  struct ip_subtree *cur=head;
  while (cur) 
    {
      mask=cur->ip_mask;
      if (cur->ip == (ip & mask))
        return cur;   /* found it */
      if (cur->ip > (ip & mask))
        cur=cur->left;
      else
        cur=cur->right;
    }
  return ((struct ip_subtree *)NULL); /* not found */
}


/*
 * delete_ip_subtree - deletes nodes that match ip and mask (ie. subsets of
 * the new mask) and appends their corresponding conf lists, if any,
 * to the clist.  returns the new head node if it was removed.
 * This routine is here so that any ambiguities can be removed before adding a
 * new entry
 * -good
 */
struct ip_subtree *delete_ip_subtree(struct ip_subtree *head,
                                     unsigned long ip,
                                     unsigned long mask,
                                     aConfItem **clist)
{
  aConfItem *cur;
  struct ip_subtree *temp, *prev;
  
  if (!head) return NULL; /* end of the tree */

  ip=ip & mask;   /* in case it wasnt masked */

  head->left=delete_ip_subtree(head->left, ip, mask, clist);
  head->right=delete_ip_subtree(head->right, ip, mask, clist);

  if ((head->ip & mask)==ip)
    {  /* IP match */
      if ((head->ip_mask) < mask)
        return head; /* but is less specific */
      /* inherit the conf list, weed through it later */
      cur=head->conf;
      while (cur->next)
        cur=cur->next;   /* cur is the last item in the list */
      cur->next=*clist;
      (*clist)=head->conf;
    
      /* remove the matching node */
      if (head->left==NULL)
        { /* no left child */
          temp=head->right;
          MyFree(head); 
          return temp;
        }
      if (head->right==NULL)
        { /* no right child */
          temp=head->left;
          MyFree(head);
          return temp;
        }

    /* flip a coin here to decide which node to replace this one with */
    if (rand() < (RAND_MAX>>1))
      {
        /* replace head with inorder predecessor */
        temp=head->left; prev=head;
        while (temp->right) { prev=temp; temp=temp->right; }
        /* temp is the inorder predecessor */
        temp->right=head->right;
        MyFree(head);
        prev->right=NULL;  /* remember to remove it too :P */
        return temp;
      }
    else
      {
        /* replace head with inorder successor */
        temp=head->right; prev=head;
        while (temp->left) { prev=temp; temp=temp->left; }
        /* temp is the inorder predecessor */
        temp->left=head->left;
        MyFree(head);
        prev->left=NULL;  /* remember to remove it too :P */
        return temp;
      }
    }
  return head;
}

/*
 * trim_Dlines - removes big D's from a conf list
 * -good
 */
aConfItem *trim_Dlines(aConfItem *cl)
{
  aConfItem *temp;
  if (!cl) return NULL;
  if (cl->flags & CONF_FLAGS_E_LINED)
    {
      cl->next=trim_Dlines(cl->next);
      return cl;
    }
  temp=cl;
  cl=cl->next;
  free_conf(temp);
  return trim_Dlines(cl);
}  


/*
 * trim_ip_Klines - removes Klines and Ilines from the list.
 * use me when a *@ kline is added and a clist has to be inherited.
 * only K/I lines that are more specific than mask will be toasted.
 * -good
 */
aConfItem *trim_ip_Klines(aConfItem *cl, int mask)
{
  aConfItem *temp;
  if (!cl) return NULL;
  if ((cl->flags & CONF_FLAGS_E_LINED))
    /* only leave Elines */
    {
      cl->next=trim_ip_Klines(cl->next, mask);
      return cl;
    }
  /* else if this mask more specific, toast it */
  if (cl->ip_mask >= mask)
    {
      temp=cl;
      cl=cl->next;
      free_conf(temp);
      return trim_ip_Klines(cl, mask);
    }
  /* was more general, leave it */
  cl->next=trim_ip_Klines(cl->next, mask);
  return cl;
}  

/*
 * trim_ip_Elines - removes any entry from the list.
 * use me when a *@ Eline is added and a clist has to be inherited.
 * only K/I/E lines that are more specific than mask will be toasted.
 * -good
 */
aConfItem *trim_ip_Elines(aConfItem *cl, int mask)
{
  aConfItem *temp;
  if (!cl) return NULL;
  /* if this mask more specific, toast it */
  if (cl->ip_mask >= mask)
    {
      temp=cl;
      cl=cl->next;
      free_conf(temp);
      return trim_ip_Elines(cl, mask);
    }
  /* was more general, leave it */
  cl->next=trim_ip_Elines(cl->next, mask);
  return cl;
}  



/*
 * destroy_ip_subtree - destroy an entire tree (Dline[x] for example)
 */
struct ip_subtree *destroy_ip_subtree(struct ip_subtree *head)
{
  aConfItem *scan;

  if (!head) return NULL;
  /* kill children */
  destroy_ip_subtree(head->left);
  destroy_ip_subtree(head->right);
  /* destroy conf list */
  while (head->conf)
    {
      scan=head->conf->next;
      free_conf(head->conf);
      head->conf=scan;
    }
  /* destroy node */
  MyFree(head);
  return NULL;
}


/*
 * find_exception - match an IP against all unplaced d-line exceptions 
 * -good
 */
aConfItem *find_exception(unsigned long ip)
{
  aConfItem *scan=leftover;
  
  while (scan)
    {
      if (scan->ip == (ip & scan->ip_mask))
        break;
      scan=scan->next;
    }
  return scan;
}


/*
 * add_dline - add's a d-line to the conf list of the parent D-line
 * if no parent D-line can be found, the d-line is added to the list
 * of unplaced d-lines, and is rescanned later by add_Dline.
 * -good
 */
void add_dline(aConfItem *conf_ptr)
{
  unsigned long host_ip;
  unsigned long host_mask;
  struct ip_subtree *node;

  host_ip = conf_ptr->ip;
  host_mask = conf_ptr->ip_mask;

  conf_ptr->status = CONF_DLINE;
  conf_ptr->flags = CONF_FLAGS_E_LINED;

  /* find the parent D-line for this exception */
  node=find_ip_subtree(Dline[host_ip>>24], host_ip);
  if (!node)
    {   /* no parent found, so add this to the leftovers list */
      conf_ptr->next = leftover;
      leftover=conf_ptr;
      return;
    }
  
  /* found the parent, now add this exception into the list after the
   * 1st entry (D)
   * don't bother checking for ambiguities.. an exception is an exception :P
   */
  conf_ptr->next=node->conf->next;
  node->conf->next=conf_ptr;      
}



/*
 * rescan_dlines - attempts to add unplaced dlines to the tree 
 * -good
 */
aConfItem *rescan_dlines(aConfItem *s)
{
  aConfItem *temp;
  if (!s) return NULL;
  s->next=rescan_dlines(s->next);
  if (find_ip_subtree(Dline[s->ip>>24],s->ip))
    { /* parent found! */
      temp=s->next;
      s->next=NULL;
      add_dline(s);
      return temp;
    }
  return s;
}


/*
 * add_Dline  - adds a D-line for ip & mask to the Dline[] table.
 * Will not add D-lines that are less broad than an existing D-line,
 * or that are covered by an unplaced d-line.
 * Less broad D-lines covered by the new D-line are removed from the
 * tree, and their from d-lines are inherited by the new D-line.
 * add_Dline also updates the oracle[] value for the appropriate tree.
 * -good
 */
void add_Dline(aConfItem *conf_ptr)
{
  unsigned long host_ip;
  unsigned long host_mask;
  aConfItem *clist = NULL;
  struct ip_subtree *node;

  host_ip = conf_ptr->ip;

  if( conf_ptr->ip_mask == 0L )
    {
      conf_ptr->ip_mask = 0xFFFFFFFFL;
    }

  host_mask = conf_ptr->ip_mask;

  /* resolve ambiguities, duplicates, etc. */

  node=find_ip_subtree(Dline[host_ip>>24], host_ip);
  if ((node) && (node->ip_mask <= host_mask)) /* found a broader Dline, dont add this one */
    return;
  /* check if this Dline is covered by an exception */
  if(find_exception(host_ip))  /* it is!  throw it away! */
     return;

  /* update the oracle's bitmask */
  oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

  /* all good so far, now remove any ambiguities
   * and collect their conf lists
   */
  Dline[host_ip>>24]=
    delete_ip_subtree(Dline[host_ip>>24], host_ip, host_mask, &clist);
  
  /* remove any D's in the list */
  clist=trim_Dlines(clist);
  
  /* add the Dline's aConfItem to the head of the conf list */

  conf_ptr->next=clist;
  clist=conf_ptr;

  /* create a new node and insert it into the tree */
  node=new_ip_subtree(host_ip, host_mask, clist, NULL, NULL);
  Dline[host_ip>>24]=insert_ip_subtree(Dline[host_ip>>24], node);

  /* last of all, rescan unplaced d-lines list in case any are now placeable */
  leftover=rescan_dlines(leftover);
}


/*
 * match_Dline - matches the specified IP against the D/d-line table
 * and returns the matching aConfItem, or NULL if no match was found.
 * -good
 */
aConfItem *match_Dline(unsigned long ip)
{
  struct ip_subtree *node;
  aConfItem *scan;
  int head=ip>>24;
  
  if ((oracle[head] & ip) != ip)    /* oracle query failed.. IP is definitely not in */
    return NULL;                    /*   this tree.  Don't even bother looking */

  /* check the top level */
  /* if (Dline[head]==NULL) return NULL; <--- oracle check should cover this */  /* no match */
  
  /* check the ip_subtree */
  node=find_ip_subtree(Dline[head], ip);
  if (!node) return NULL;   /* no match */
  
  /* scan for exceptions */
  for(scan=node->conf;scan;scan=scan->next)
    {
      if(!(scan->flags & CONF_FLAGS_E_LINED))
         continue;

      if (scan->ip == (ip & scan->ip_mask))
        return (scan);   /* exception found */
    }
  if( node->conf )
    {
      struct ConfItem *conf_ptr;
      conf_ptr = node->conf;
      if ( (conf_ptr->ip & conf_ptr->ip_mask) ==
           (ip           & conf_ptr->ip_mask) )
        {
          return(conf_ptr);
	}
      else
        return NULL;
    }
   return NULL;
}

/*
 * add_ip_Kline  - modified form of add_Dline
 * -good
 */
void add_ip_Kline(aConfItem *conf_ptr)
{
  unsigned long host_ip;
  unsigned long host_mask;
  aConfItem *clist = NULL;
  aConfItem *scan = NULL;
  struct ip_subtree *node;

  host_ip = conf_ptr->ip;

  if( conf_ptr->ip_mask == 0L )
    {
      conf_ptr->ip_mask = 0xFFFFFFFFL;
    }
  
  host_mask = conf_ptr->ip_mask;

  /* resolve ambiguities, duplicates, etc. */

  /* check for existing ip/mask */
  node=find_ip_subtree(ip_Kline[host_ip>>24], host_ip);

  if (!node)
    { /* none exist, gather up the lesser ones and add a new node */
      /* update oracle */
      ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

      /* now collect nodes with more specific ip masks */
      /* #if 0 */
      ip_Kline[host_ip>>24]=
        delete_ip_subtree(ip_Kline[host_ip>>24], host_ip, host_mask, &clist);
      /* #endif */

      /* if this is a *@ Kline, then we can toast all the other Klines in the clist */
      if (!(strcmp(conf_ptr->user,"*")))
        clist=trim_ip_Klines(clist,0);

      conf_ptr->next=clist;
      clist=conf_ptr;
 
      /* create a new node and insert it into the tree */
      node=new_ip_subtree(host_ip, host_mask, clist, NULL, NULL);
      ip_Kline[host_ip>>24]=insert_ip_subtree(ip_Kline[host_ip>>24], node);
      return;  /* done */
    }
  
  /* otherwise, this Kline is being placed in an existing list */
  /* if there's a more general *@ Kline (or Eline) in the list, then dont bother adding */
  for (scan=node->conf;scan;scan=scan->next)
    {
      if (((scan->status & CONF_KILL) || (scan->flags & CONF_FLAGS_E_LINED)) &&
          (!(strcmp(scan->user,"*"))) &&
          (scan->ip_mask<host_mask)) break;
    }
  if (scan) return; /* don't bother adding */

  /* update oracle */
  ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

  if (strcmp(conf_ptr->user,"*")) 
    {  /* not adding a *@ Kline, just slap it in */
      conf_ptr->next=node->conf;
      node->conf=conf_ptr;
      return;
    }

  /* final possibility, we're adding a *@ to the list, only toast more
     specific I/K lines */
  node->conf=trim_ip_Klines(node->conf, host_mask);
  conf_ptr->next=node->conf;
  node->conf=conf_ptr;
  return; /* done, whew */
}



/*
 * add_ip_Eline  - modified form of add_Dline
 * -good
 */
void add_ip_Eline(aConfItem *conf_ptr)
{
  unsigned long host_ip;
  unsigned long host_mask;
  aConfItem *clist = NULL;
  aConfItem *scan = NULL;
  struct ip_subtree *node;

  host_ip = conf_ptr->ip;
  host_mask = conf_ptr->ip_mask;

  /* resolve ambiguities, duplicates, etc. */

  /* check for existing ip/mask */
  node=find_ip_subtree(ip_Kline[host_ip>>24], host_ip);

  if (!node)
    { /* none exist, gather up the lesser ones and add a new node */
      /* update oracle */
      ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

      /* now collect nodes with more specific ip masks */

      ip_Kline[host_ip>>24]=
        delete_ip_subtree(ip_Kline[host_ip>>24], host_ip, host_mask, &clist);

      /* if this is a *@ Eline, then we can toast all the others in the clist */
      if (!(strcmp(conf_ptr->user,"*"))) 
        clist=trim_ip_Elines(clist,0);

      conf_ptr->next=clist;
      clist=conf_ptr;
 
      /* create a new node and insert it into the tree */
      node=new_ip_subtree(host_ip, host_mask, clist, NULL, NULL);
      ip_Kline[host_ip>>24]=insert_ip_subtree(ip_Kline[host_ip>>24], node);
      return;  /* done */
    }
  
  /* otherwise, this Eline is being placed in an existing list */
  /* if there's a more general *@ Eline in the list, then dont bother adding */
  for (scan=node->conf;scan;scan=scan->next)
    {
      if ((scan->flags & CONF_FLAGS_E_LINED) &&
          (!(strcmp(scan->user,"*"))) &&
          (scan->ip_mask<host_mask)) break;
    }
  if (scan) return; /* don't bother adding */

  /* update oracle */
  ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

  if (strcmp(conf_ptr->user,"*")) 
    {  /* not adding a *@ Eline, just slap it in */
      conf_ptr->next=node->conf;
      node->conf=conf_ptr;
      return;
    }

  /* final possibility, we're adding a *@ to the list, only toast more
     specific lines */
  node->conf=trim_ip_Elines(node->conf, host_mask);
  conf_ptr->next=node->conf;
  node->conf=conf_ptr;
  return; /* done, whew */
}


#if 0
/* lets add this later, when sure its fully debugged etc. */

/*
 * add_ip_Iline  - modified form of add_Dline
 * -good
 */
void add_ip_Iline(aConfItem *conf_ptr)
{
  unsigned long host_ip;
  unsigned long host_mask;
  aConfItem *clist = NULL;
  struct ip_subtree *node;

  host_ip = conf_ptr->ip;
  host_mask = conf_ptr->ip_mask;

  /* resolve ambiguities, duplicates, etc. */

  /* check for existing ip/mask */
  node=find_ip_subtree(ip_Kline[host_ip>>24], host_ip);

  if (!node)
    { /* none exist, gather up the lesser ones and add a new node */
      /* update oracle */
      ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);

      /* now collect nodes with more specific ip masks */

      ip_Kline[host_ip>>24]=
        delete_ip_subtree(ip_Kline[host_ip>>24], host_ip, host_mask, &clist);


      /* insert the Iline */
      conf_ptr->next=clist;
      clist=conf_ptr;
 
      /* create a new node and insert it into the tree */
      node=new_ip_subtree(host_ip, host_mask, clist, NULL, NULL);
      ip_Kline[host_ip>>24]=insert_ip_subtree(ip_Kline[host_ip>>24], node);
      return;  /* done */
    }
  
  /* otherwise, this Iline is being placed in an existing list */
  /* update oracle */
  ike_oracle[host_ip>>24] |= ((0xffffffff-host_mask)+host_ip);
  
  conf_ptr->next=node->conf;
  node->conf=conf_ptr;
  return;
}
#endif


/*
 * match_ip_Kline - matches the specified IP against the K/E/I-line table
 * and returns the matching aConfItem, or NULL if no match was found.
 * -good
 */
aConfItem* match_ip_Kline(unsigned long ip, const char* name)
{
  struct ip_subtree* node;
  aConfItem*         scan;
  int                head = ip >> 24;
  aConfItem*         winner;
  char               winnertype;
  if ((ike_oracle[head] & ip) != ip) 
   /* 
    * oracle query failed.. IP is definitely not in
    *   this tree.  Don't even bother looking 
    */
    return NULL;

  /* check the top level */
  if (ip_Kline[head]==NULL) return NULL;
  
  /* check the ip_subtree */
  node=find_ip_subtree(ip_Kline[head], ip);
  if (!node) return NULL;   /* no match */
  
  if(!name)
    return((aConfItem *)NULL);

  winner=NULL;     /* no matches yet */
  winnertype='I';  /* default to Iline, since they suck */
  /* rules are simple, Iline wins by default, K beats I, E beats K and I */
  for( scan=node->conf; scan; scan=scan->next)
    {
      if (scan->ip != (ip & scan->ip_mask))
        continue; /* Not even in the same ball park */

      if (scan->flags & CONF_FLAGS_E_LINED)  /* Eline */
        if (match(scan->user,name)) return scan; /* instant win! */

      if (scan->status & CONF_KILL)  /* Kline */
        if (match(scan->user,name))
          {
            winnertype='K'; winner=scan;
            continue;
          }

      if (scan->status & CONF_CLIENT) /* Iline */
        if (match(scan->user,name)) /* name jives */
          if (winnertype=='I') /* we might be able to beat the champ */
            {
              if (winner && (scan->ip_mask <= winner->ip_mask))
                {
                  continue;  /* more vague */
                }
              else
                {
                  winner=scan;
                }
            }
    }
  return winner;
}


/* clears the D/d-line table, as well as E/I/K table */
void clear_Dline_table()
{
  memset((void *)oracle, 0, sizeof(oracle));
  memset((void *)ike_oracle, 0, sizeof(oracle));
  memset((void *)Dline, 0, sizeof(Dline));
  memset((void *)ip_Kline, 0, sizeof(ip_Kline));
  leftover=NULL;
}


/*
 * zap_Dlines - clears out the entire Dline/ip_Kline structure.
 * (use this to init the tables too)us
 * -good
 */
void zap_Dlines() 
{
  int i;
  aConfItem *s, *ss;
  for (i=0; i<256; i++)
    {
      oracle[i]=0;  /* clear the oracle field */
      Dline[i] = destroy_ip_subtree(Dline[i]);   /* kill the tree */
    }

  for (i=0; i<256; i++)
    {
      ike_oracle[i]=0;  /* clear the oracle field */
      ip_Kline[i] = destroy_ip_subtree(ip_Kline[i]);   /* kill the tree */
    }
  s=leftover;

  while (s)
    {    /* toast the leftovers list */
      ss=s->next;
      free_conf(s);
      s=ss;
    }
  leftover=NULL;
}

/*
 * walk_the_dlines - inorder traversal of a Dline tree, printing Dlines and
 * exceptions.
 * -good
 */
void
walk_the_dlines(struct Client *sptr, struct ip_subtree *tree)
{
  struct ConfItem *scan;
  char *name, *host, *pass, *user;
  int port;
  char c;               /* D,d or K */

  if (tree == NULL)
    return;
  
  /* do an inorder traversal of the tree */
  walk_the_dlines(sptr, tree->left);
  scan=tree->conf;

  for(scan=tree->conf;scan;scan=scan->next)
    {
      if(!(scan->status & CONF_DLINE))
        continue;

      c = 'D';
      if(scan->flags & CONF_FLAGS_E_LINED)
        c = 'd';
      /* print Dline */

      get_printable_conf(scan, &name, &host, &pass, &user, &port);

      sendto_one(sptr, form_str(RPL_STATSDLINE), me.name,
                 sptr->name, c, host, pass);
    }
  walk_the_dlines(sptr, tree->right);
}

void report_dlines(aClient *sptr)
{
  int i;
  for (i=0;i<256;i++) walk_the_dlines(sptr, Dline[i]);
}


/*
 * walk_the_ip_Klines - inorder traversal of a Dline tree, printing K/I/Elines
 * -good
 */
void
walk_the_ip_Klines(struct Client *sptr, struct ip_subtree *tree, 
                        char conftype, int MASK)
{
  struct ConfItem *scan;
  char *name, *host, *pass, *user;
  int port;

  if (!tree) return;
  
  /* do an inorder traversal of the tree */
  walk_the_ip_Klines(sptr, tree->left, conftype, MASK);
  scan=tree->conf;
  for(scan=tree->conf;scan;scan=scan->next)
    {
      if((scan->status & MASK) == 0)
        continue;

      get_printable_conf(scan, &name, &host, &pass, &user, &port);

      if(scan->status & CONF_KILL)
        {
          /* print Kline */
          
          sendto_one(sptr, form_str(RPL_STATSKLINE), me.name,
                     sptr->name, conftype, host, user, pass);
        }
      else if(scan->status & CONF_CLIENT)
        {
          /* print IP I line */

          sendto_one(sptr, form_str(RPL_STATSILINE), me.name,
                     sptr->name,
                     'I',
                     name,
                     show_iline_prefix(sptr,scan,user),
                     host,
                     port,
                     get_conf_class(scan));
        }
    }
  walk_the_ip_Klines(sptr, tree->right, conftype, MASK);
}

void 
report_ip_Klines(aClient *sptr)
{
  int i;
  for (i=0;i<256;i++) walk_the_ip_Klines(sptr, ip_Kline[i],'K', CONF_KILL);
}


void
report_ip_Ilines(aClient *sptr)
{
  int i;
  for (i=0;i<256;i++) walk_the_ip_Klines(sptr, ip_Kline[i],'I', CONF_CLIENT);
}
