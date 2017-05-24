/************************************************************************
 *   IRC - Internet Relay Chat, src/dbuf.c
 *   Copyright (C) 1990 Markku Savela
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
 * For documentation of the *global* functions implemented here,
 * see the header file (dbuf.h).
 *
 *
 * $Id: dbuf.c,v 1.1.1.1 2006/03/08 23:28:08 malign Exp $
 */
#include "dbuf.h"
#include "common.h"
#include "irc_string.h"
#include "ircd_defs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


/*
 * And this 'DBufBuffer' should never be referenced outside the
 * implementation of 'dbuf'--would be "hidden" if C had such
 * keyword...
 * doh!!! ya just gotta know how to do it ;-)
 */
/*
 * DBUF_SIZE must be a power of 2 so we can mask for the offset
 */
/* 2k is a real pig with bigger servers */
/* #define DBUF_SIZE 2048 */
#define DBUF_SIZE 1024

#define DBUF_USUAL_MAX_COUNT (BUFFERPOOL/DBUF_SIZE)

struct DBufBuffer {
  struct DBufBuffer* next;             /* Next data buffer, NULL if last */
  char*              start;            /* data starts here */
  char*              end;              /* data ends here */ 
  char               data[DBUF_SIZE];  /* Actual data stored here */
};

int                       DBufUsedCount = 0;
int                       DBufCount = 0;
static struct DBufBuffer* dbufFreeList = NULL;

void count_dbuf_memory(size_t* allocated, size_t* used)
{
  assert(allocated != NULL);
  assert(used != NULL);
  *allocated = DBufCount     * sizeof(struct DBufBuffer);
  *used      = DBufUsedCount * sizeof(struct DBufBuffer);
}

/* 
 * dbuf_init--initialize a stretch of memory as dbufs.
 * Doing this early on should save virtual memory if not real memory..
 * at the very least, we get more control over what the server is doing 
 * 
 * mika@cs.caltech.edu 6/24/95
 *
 * XXX - Unfortunately this makes cleanup impossible because the block 
 * pointer isn't saved and dbufs are not allocated in chunks anywhere else.
 * --Bleep
 */
void dbuf_init()
{
  int      i;
  struct DBufBuffer* current_bp;
  struct DBufBuffer* new_bp;

  assert(dbufFreeList == NULL);

  dbufFreeList =
    (struct DBufBuffer*) MyMalloc(sizeof(struct DBufBuffer));
  assert(dbufFreeList != NULL);

  current_bp = dbufFreeList;

  for (i = 0; i < INITIAL_DBUFS; i++ )
  {
    new_bp = (struct DBufBuffer*) MyMalloc(sizeof(struct DBufBuffer));

    current_bp->next = new_bp;
    current_bp = new_bp;
  }
  current_bp->next = 0;

  DBufCount = INITIAL_DBUFS;
}

/*
 * dbuf_alloc - allocates a struct DBufBuffer structure either from 
 * dbufFreeList or create a new one.
 */
static struct DBufBuffer* dbuf_alloc(void)
{
  struct DBufBuffer* db = dbufFreeList;

  if (db)
    dbufFreeList = dbufFreeList->next;
  else
  {
    db = (struct DBufBuffer*) MyMalloc(sizeof(struct DBufBuffer));
    assert(db != NULL);
    ++DBufCount;
  }
  ++DBufUsedCount;

  db->next  = 0;
  db->start = db->end = db->data;
  return db;
}

/*
 * dbuf_free - return a struct DBufBuffer structure to the dbufFreeList
 */
static void dbuf_free(struct DBufBuffer* ptr)
{
  assert(ptr != NULL);
  assert(DBufUsedCount > 0);

  if (DBufUsedCount > DBUF_USUAL_MAX_COUNT)
  {
    MyFree(ptr);
  }
  else
  {
    ptr->next = dbufFreeList;
    dbufFreeList = ptr;
  }
  --DBufUsedCount;
}
/*
** This is called when malloc fails. Scrap the whole content
** of dynamic buffer and return -1. (malloc errors are FATAL,
** there is no reason to continue this buffer...). After this
** the "dbuf" has consistent EMPTY status... ;)
*/
static int dbuf_malloc_error(struct DBuf* dyn)
{
  struct DBufBuffer* db;

  dyn->length = 0;
  while (0 != (db = dyn->head))
  {
    dyn->head = db->next;
    dbuf_free(db);
  }
  dyn->tail = 0;
  return 0;
}

/*
 * dbuf_put - put a sequence of bytes in a dbuf
 */
int dbuf_put(struct DBuf* dyn, const char* buf, size_t length)
{
  struct DBufBuffer** h;
  struct DBufBuffer*  d;
  int                 chunk;

  assert(dyn != NULL);
  assert(buf != NULL);
  /*
   * Locate the last non-empty buffer. If the last buffer is
   * full, the loop will terminate with 'd==NULL'. This loop
   * assumes that the 'dyn->length' field is correctly
   * maintained, as it should--no other check really needed.
   */
  if (0 == dyn->length)
    h = &(dyn->head);
  else
    h = &(dyn->tail);
  /*
   * Append users data to buffer, allocating buffers as needed
   */
  dyn->length += length;

  for ( ; length > 0; h = &(d->next))
  {
    if (0 == (d = *h))
    {
      if (0 == (d = dbuf_alloc()))
        return dbuf_malloc_error(dyn);

      dyn->tail = d;
      *h        = d;        /* prev->next = d */
    }
    chunk = (d->data + DBUF_SIZE) - d->end;
    if (chunk != 0)
    {
      if (chunk > length)
        chunk = length;
      
      memcpy(d->end, buf, chunk);

      length -= chunk;
      buf    += chunk;
      d->end += chunk;
    }
  }
  return 1;
}


const char* dbuf_map(const struct DBuf* dyn, size_t* length)
{
  assert(dyn != NULL);

  if (0 == dyn->length)
  {
    *length   = 0;
    return 0;
  }
  assert(dyn->head != NULL);

  *length = dyn->head->end - dyn->head->start;
  return dyn->head->start;
}


void dbuf_delete(struct DBuf* dyn, size_t length)
{
  struct DBufBuffer* db;
  size_t             chunk;

  assert(dyn != NULL);

  if (length > dyn->length)
    length = dyn->length;

  while (length > 0)
  {
    if (0 == (db = dyn->head))
      break;
    chunk = db->end - db->start; 
    if (chunk > length)
      chunk = length;

    length      -= chunk;
    dyn->length -= chunk;
    db->start   += chunk;

    if (db->start == db->end)
    {
      dyn->head = db->next;
      dbuf_free(db);
    }
  }
  if (0 == dyn->head)
  {
    dyn->tail   = 0;
    dyn->length = 0;
  }
}

size_t dbuf_get(struct DBuf* dyn, char* buf, size_t length)
{
  size_t      moved = 0;
  size_t      chunk;
  const char* b;

  assert(0 != dyn);
  assert(0 != buf);

  while (length > 0 && (b = dbuf_map(dyn, &chunk)) != 0)
  {
    if (chunk > length)
      chunk = length;

    memcpy(buf, b, chunk);
    dbuf_delete(dyn, chunk);

    buf    += chunk;
    length -= chunk;
    moved  += chunk;
  }
  return moved;
}

static size_t dbuf_flush(struct DBuf* dyn)
{
  struct DBufBuffer* db = dyn->head;
  
  if (0 == db)
    return 0;

  assert(db->start < db->end);
  /*
   * flush extra line terms
   */
  while (IsEol(*db->start))
  {
    if (++db->start == db->end)
    {
      dyn->head = db->next;
      dbuf_free(db);
      if (0 == (db = dyn->head))
      {
        dyn->tail   = 0;
        dyn->length = 0;
        break;
      }
    }
    --dyn->length;
  }
  return dyn->length;
}

/*
 * dbuf_getmsg
 *
 * Check the buffers to see if there is a string which is terminated with
 * either a \r or \n present.  If so, copy as much as possible (determined by
 * length) into buf and return the amount copied - else return 0.
 *
 * There may be cr/lf pairs or leading garbage in the dbuf to start with
 *
 * case 1:
 *   data starts at buf + offset, entire message fits in a dbuf
 * case 2:
 *   data starts at buf + offset, more than one dbuf is spanned
 */
int dbuf_getmsg(struct DBuf* dyn, char* buf, size_t length)
{
  struct DBufBuffer* db;
  char*              start;
  char*              end;
  size_t             count;
  size_t             copied = 0;

  assert(dyn != NULL);
  assert(buf != NULL);

  if (0 == dbuf_flush(dyn))
    return 0;

  assert(dyn->head != NULL);
  
  db    = dyn->head;
  start = db->start;

  assert(start < db->end);

  if (length > dyn->length)
    length = dyn->length;
  /*
   * might as well copy it while we're here
   */
  while (length > 0)
  {
    end = IRCD_MIN(db->end, (start + length));
    while (start < end && !IsEol(*start))
      *buf++ = *start++;

    count = start - db->start;
    if (start < end)
    {
      *buf = '\0';
      copied += count;
      dbuf_delete(dyn, copied);
      dbuf_flush(dyn);
      return copied;
    } 
    if (0 == (db = db->next))
      break;
    copied += count;
    length -= count;
    start = db->start;
  }
  return 0;  
}


