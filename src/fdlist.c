/* 
 *
 * fdlist.c   maintain lists of certain important fds 
 *
 *
 * $Id: fdlist.c,v 1.1.1.1 2006/03/08 23:28:08 malign Exp $
 */
#include "fdlist.h"
#include "client.h"  /* struct Client */
#include "ircd.h"    /* GlobalSetOptions */
#include "res.h"
#include "s_bsd.h"   /* highest_fd */
#include "config.h"  /* option settings */
#include <string.h>
#include <assert.h>

unsigned char GlobalFDList[MAXCONNECTIONS + 1];

void fdlist_init(void)
{
  static int initialized = 0;
  assert(0 == initialized);
  if (!initialized) {
    memset(GlobalFDList, 0, sizeof(GlobalFDList));
    initialized = 1;
  }
}

void fdlist_add(int fd, unsigned char mask)
{
  assert(fd < MAXCONNECTIONS + 1);
  GlobalFDList[fd] |= mask;
}
 
void fdlist_delete(int fd, unsigned char mask)
{
  assert(fd < MAXCONNECTIONS + 1);
  GlobalFDList[fd] &= ~mask;
}

#ifndef NO_PRIORITY
#ifdef CLIENT_SERVER
#define BUSY_CLIENT(x) \
    (((x)->priority < 55) || (!GlobalSetOptions.lifesux && ((x)->priority < 75)))
#else
#define BUSY_CLIENT(x) \
    (((x)->priority < 40) || (!GlobalSetOptions.lifesux && ((x)->priority < 60)))
#endif
#define FDLISTCHKFREQ  2

/*
 * This is a pretty expensive routine -- it loops through
 * all the fd's, and finds the active clients (and servers
 * and opers) and places them on the "busy client" list
 */
void fdlist_check(time_t now)
{
  struct Client* cptr;
  int            i;

  for (i = highest_fd; i >= 0; --i)
    {

      if (!(cptr = local[i])) 
        continue;
      if (IsServer(cptr) || IsEmpowered(cptr))
          continue;

      GlobalFDList[i] &= ~FDL_BUSY;
      if (cptr->receiveM == cptr->lastrecvM)
        {
          cptr->priority += 2;  /* lower a bit */
          if (90 < cptr->priority) 
            cptr->priority = 90;
          else if (BUSY_CLIENT(cptr))
            {
              GlobalFDList[i] |= FDL_BUSY;
            }
          continue;
        }
      else
        {
          cptr->lastrecvM = cptr->receiveM;
          cptr->priority -= 30; /* active client */
          if (cptr->priority < 0)
            {
              cptr->priority = 0;
              GlobalFDList[i] |= FDL_BUSY;
            }
          else if (BUSY_CLIENT(cptr))
            {
              GlobalFDList[i] |= FDL_BUSY;
            }
        }
    }
}
#endif

