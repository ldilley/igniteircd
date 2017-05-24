/*
 * contributed by mjr
 * $Id: ircd_start.c,v 1.1.1.1 2006/03/08 23:28:14 malign Exp $
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/resource.h>

#define IRCD_EXECUTABLE "/usr/local/bin/ircd" /* location of executable    */
#define IRCD_USER       "ircd"                 /* user to setuid to b4 fork */

#define SOFT_FD_NUM     4096                  /* default soft fd limit     */
#define HARD_FD_NUM     8096                  /* default hard fd limit     */

int usage() {
        fprintf(stderr, "usage: ircd_start [ -h ] [ -c rlim_fd_cur ] ");
        fprintf(stderr, "[ -m rlim_fd_max ]\n");
        fprintf(stderr, "        -h   this usage information\n");
        fprintf(stderr, "        -c   set the low fd limit (currently: %d)\n",
                                SOFT_FD_NUM);
        fprintf(stderr, "        -m   set the high fd limit (currently: %d)\n",
                                HARD_FD_NUM);
        exit(0);
}

int main(int argc, char *argv[]) {
        int             ch, ret, rlim_fd_cur, rlim_fd_max;
        struct rlimit   rlp;
        struct passwd   *pw;
        extern char     *optarg;
        extern int      optind, opterr, optopt;

        rlim_fd_cur = SOFT_FD_NUM;
        rlim_fd_max = HARD_FD_NUM;

        while((ch = getopt(argc, argv, "c:m:h")) != EOF) {
                switch(ch) {
                        case 'c':
                                rlim_fd_cur = atoi(optarg);
                                break;
                        case 'm':
                                rlim_fd_max = atoi(optarg);
                                break;
                        case 'h':
                        case '?':
                        default:
                                usage();
                                /* not reached */
                }
        }
        argc -= optind;

        if (argc != 0) usage();

        rlp.rlim_cur = rlim_fd_cur;
        rlp.rlim_max = rlim_fd_max;

        if (( ret = setrlimit(RLIMIT_NOFILE, &rlp) ) < 0 ) {
                fprintf(stderr, "%s: unable to set resource limits (%s)\n",
                                                argv[0], strerror(errno));
                exit(ret);
        }

        /* check if user exists in the first place */
        if ((pw = getpwnam(IRCD_USER)) == (struct passwd *) NULL) {
                fprintf(stderr, "%s: no such user %s\n", argv[0], IRCD_USER);
                exit(-1);
        }

        /* user does exist, time to switch to their uid/gid */
        setuid(pw->pw_uid);
        setgid(pw->pw_gid);

        /* attempt to spawn given executable */
        if ((ret = execl(IRCD_EXECUTABLE, IRCD_EXECUTABLE, 0)) < 0) {
                fprintf(stderr, "%s: execl() failed for %s (reason: %s)\n",
                                argv[0], IRCD_EXECUTABLE, strerror(errno));
                exit(ret);
        }

        return(0);
}

