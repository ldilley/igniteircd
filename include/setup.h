/* include/setup.h.  Generated automatically by configure.  */
/* $Id: setup.h.in,v 1.1.1.1 2006/03/08 23:28:07 malign Exp $ */
/* include/setup.h.in.  Generated automatically from autoconf/configure.in by autoheader 2.13.  */

/* Define if you have a working `mmap' system call.  */
#define HAVE_MMAP 1

/* Define as __inline if that's what the C compiler calls it.  */
/* #undef inline */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define only one of POSIX, BSD, or SYSV signals.  */
/* Define if your system has reliable POSIX signals.  */
/* #undef POSIX_SIGNALS */

/* Define if your system has reliable BSD signals.  */
#define BSD_RELIABLE_SIGNALS 1

/* Define if your system has unreliable SYSV signals.  */
/* #undef SYSV_UNRELIABLE_SIGNALS */

/* Define if you have the poll() system call.  */
#define USE_POLL 1

/* Chose only one of NBLOCK_POSIX, NBLOCK_BSD, and NBLOCK_SYSV */
/* Define if you have posix non-blocking sockets (O_NONBLOCK) */
#define NBLOCK_POSIX 1

/* Define if you have BSD non-blocking sockets (O_NDELAY) */
/* #undef NBLOCK_BSD */

/* Define if you have SYSV non-blocking sockets (FIONBIO) */
/* #undef NBLOCK_SYSV */

/* Define if you have the getpagesize function.  */
#define HAVE_GETPAGESIZE 1

/* Define if you have the <crypt.h> header file.  */
#define HAVE_CRYPT_H 1

/* Define if you have the <errno.h> header file.  */
#define HAVE_ERRNO_H 1

/* Define if you have the <getopt.h> header file.  */
#define HAVE_GETOPT_H 1

/* Define if you have the <stddef.h> header file.  */
#define HAVE_STDDEF_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/param.h> header file.  */
#define HAVE_SYS_PARAM_H 1

/* Define if you have the <sys/select.h> header file.  */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the descrypt library (-ldescrypt).  */
/* #undef HAVE_LIBDESCRYPT */

/* Define if you have the nsl library (-lnsl).  */
/* #undef HAVE_LIBNSL */

/* Define if you have the resolv library (-lresolv).  */
/* #undef HAVE_LIBRESOLV */

/* Define if you have the socket library (-lsocket).  */
/* #undef HAVE_LIBSOCKET */

/* Define if you have the z library (-lz).  */
#define HAVE_LIBZ 1
