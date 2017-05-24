/*
 * motd.h
 *
 * $Id: motd.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 */
#ifndef INCLUDED_motd_h
#define INCLUDED_motd_h
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"    /* MAX_DATE_STRING */
#endif
#ifndef INCLUDED_limits_h
#include <limits.h>       /* PATH_MAX */
#define INCLUDED_limits_h
#endif

#define MESSAGELINELEN 89       

typedef enum {
  USER_MOTD,
  OPER_MOTD,
  HELP_MOTD
} MotdType;

struct MessageFileLine
{
  char                    line[MESSAGELINELEN + 1];
  struct MessageFileLine* next;
};

typedef struct MessageFileLine MessageFileLine;

struct MessageFile
{
  char             fileName[PATH_MAX + 1];
  MotdType         motdType;
  MessageFileLine* contentsOfFile;
  char             lastChangedDate[MAX_DATE_STRING + 1];
};

typedef struct MessageFile MessageFile;

struct Client;

void InitMessageFile(MotdType, char *, struct MessageFile *);
int SendMessageFile(struct Client *, struct MessageFile *);
int ReadMessageFile(MessageFile *);

#endif /* INCLUDED_motd_h */
