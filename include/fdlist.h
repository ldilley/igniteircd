/*
 * fdlist.h
 *
 * $Id: fdlist.h,v 1.1.1.1 2006/03/08 23:28:06 malign Exp $
 */
#ifndef INCLUDED_fdlist_h
#define INCLUDED_fdlist_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>         /* time_t */
#define INCLUDED_sys_types_h
#endif

extern unsigned char GlobalFDList[];

/*
 * priority values used in fdlist code
 */
#define FDL_SERVER   0x01
#define FDL_BUSY     0x02
#define FDL_OPER     0x04
#define FDL_DEFAULT  0x08 
#define FDL_ALL      0xFF

void fdlist_add(int fd, unsigned char mask);
void fdlist_delete(int fd, unsigned char mask);
void fdlist_init(void);
void fdlist_check(time_t now);

#endif /* INCLUDED_fdlist_h */

