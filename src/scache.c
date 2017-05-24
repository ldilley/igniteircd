/*
 * scache.c
 *
 * $Id: scache.c,v 1.1.1.1 2006/03/08 23:28:13 malign Exp $
 */

#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "struct.h"
#include "scache.h"


#include <assert.h>
#include <string.h>



/*
 * ircd used to store full servernames in anUser as well as in the 
 * whowas info.  there can be some 40k such structures alive at any
 * given time, while the number of unique server names a server sees
 * in its lifetime is at most a few hundred.  by tokenizing server
 * names internally, the server can easily save 2 or 3 megs of RAM.  
 * -orabidoo
 */


#define SCACHE_HASH_SIZE 257

typedef struct scache_entry
{
  char name[HOSTLEN+1];
  struct scache_entry *next;
} SCACHE;

static SCACHE *scache_hash[SCACHE_HASH_SIZE];

void clear_scache_hash_table(void)
{
  memset(scache_hash, 0, sizeof(scache_hash));
}

static int hash(const char* string)
{
  int hash_value;

  hash_value = 0;
  while (*string)
    {
      hash_value += ToLower(*string);
      /* I don't like auto increments inside macro calls... -db */
      string++;
    }

  return hash_value % SCACHE_HASH_SIZE;
}

/*
 * this takes a server name, and returns a pointer to the same string
 * (up to case) in the server name token list, adding it to the list if
 * it's not there.  care must be taken not to call this with
 * user-supplied arguments that haven't been verified to be a valid,
 * existing, servername.  use the hash in list.c for those.  -orabidoo
 */

const char* find_or_add(const char* name)
{
  int     hash_index;
  SCACHE* ptr;

  ptr = scache_hash[hash_index = hash(name)];
  for ( ; ptr; ptr = ptr->next) 
    {
      if (!irccmp(ptr->name, name))
        return(ptr->name);
    }

  ptr = (SCACHE*) MyMalloc(sizeof(SCACHE));
  assert(0 != ptr);

  strncpy_irc(ptr->name, name, HOSTLEN);
  ptr->name[HOSTLEN] = '\0';

  ptr->next = scache_hash[hash_index];
  scache_hash[hash_index] = ptr;
  return ptr->name;  
}

/* Added so s_debug could check memory usage in here -Dianora */

void count_scache(int *number_servers_cached,u_long *mem_servers_cached)
{
  SCACHE *scache_ptr;
  int i;

  *number_servers_cached = 0;
  *mem_servers_cached = 0;

  for(i = 0; i < SCACHE_HASH_SIZE ;i++)
    {
      scache_ptr = scache_hash[i];
      while(scache_ptr)
        {
          *number_servers_cached = *number_servers_cached + 1;
          *mem_servers_cached = *mem_servers_cached +
            (strlen(scache_ptr->name) +
             sizeof(SCACHE *));

          scache_ptr = scache_ptr->next;
        }
    }
}

/* list all server names in scache very verbose */
   
void list_scache(aClient *cptr,aClient *sptr,int parc,char *parv[])
{
  int hash_index;
  SCACHE *ptr;

  for (hash_index = 0; hash_index < SCACHE_HASH_SIZE ;hash_index++)
    {
      ptr = scache_hash[hash_index];
      while(ptr)
        {
          if(ptr->name)
            sendto_one(sptr,":%s NOTICE %s :%s",
                       me.name, parv[0], ptr->name);
          ptr = ptr->next;
        }
    }

}
