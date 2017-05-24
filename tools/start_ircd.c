#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

/*
  start_ircd

  This is a setuid root program to set all limits to maximum before
launching ircd.


Bugs: has holes, but everyone on here has root, so I'm not worrying about it.

Diane Bruce, db May 11 1998
*/

static
char *prog_version = "$Id: start_ircd.c,v 1.1.1.1 2006/03/08 23:28:14 malign Exp $";

#define IRCDDIR "/usr/local/lib/ircd"

main()
{
  int result;
  struct rlimit rlp;
  struct passwd   *pw;

  result = chdir(IRCDDIR);

  if(result < 0 )
    {
      (void)fprintf(stderr,"Can't chdir to %s\n", IRCDDIR);
      exit(0);
    }

  rlp.rlim_cur = RLIM_INFINITY;
  rlp.rlim_max = RLIM_INFINITY;

  result =  setrlimit(RLIMIT_CORE, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_CORE\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_CPU, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_CPU\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_DATA, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_DATA\n");
      exit(-1);
    } 

  result =  setrlimit(RLIMIT_RSS, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_RSS\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_FSIZE, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_FSIZE\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_NOFILE, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_NOFILE\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_NPROC, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_NPROC\n");
      exit(-1);
    }

  result =  setrlimit(RLIMIT_STACK, &rlp);
  if(result<0)
    {
      (void)fprintf(stderr,"Error setting RLIMIT_STACK\n");
      exit(-1);
    }

  (void)fprintf(stderr,"About to exec ircd\n");

  /* check if user exists in the first place */
  if ((pw = getpwnam(IRCD_USER)) == (struct passwd *) NULL)
    {
      fprintf(stderr, "%s: no such user %s\n", argv[0], IRCD_USER);
      exit(-1);
    }
  
  /* user does exist, time to switch to their uid/gid */
  setuid(pw->pw_uid);
  setgid(pw->pw_gid);

  result = execl("./ircd","ircd",(char *)NULL);
  
  /* shouldn't be here */

  if(result < 0)
    {
      fprintf(stderr,"Can't execute ircd\n");
      exit(0);
    }
}
