/************************************************************************
 *   IRC - Internet Relay Chat, src/ircd.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 * $Id: ircd.c,v 1.1.1.1 2006/03/08 23:28:09 malign Exp $
 */
#include "ircd.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd_signal.h"
#include "list.h"
#include "m_gline.h"
#include "motd.h"
#include "msg.h"         /* msgtab */
#include "mtrie_conf.h"
#include "numeric.h"
#include "parse.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_debug.h"
#include "s_log.h"
#include "s_misc.h"
#include "s_serv.h"      /* try_connections */
#include "s_stats.h"
#include "s_zip.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"
#include "blalloc.h"
#include "version.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/param.h>
#include <signal.h>

#if defined(HAVE_GETOPT_H)
#include <getopt.h>
#endif /* HAVE_GETOPT_H */

/*
 * for getopt
 * ZZZ this is going to need confirmation on other OS's
 *
 * #include <getopt.h>
 * Solaris has getopt.h, you should too... hopefully
 * BSD declares them in stdlib.h
 * extern char *optarg;
 * 
 * for FreeBSD the following are defined:
 *
 * extern char *optarg;
 * extern int optind;
 * extern in optopt;
 * extern int opterr;
 * extern in optreset;
 */

/*
 * Try and find the correct name to use with getrlimit() for setting the max.
 * number of files allowed to be open by this process.
 */
#ifdef RLIMIT_FDMAX
# define RLIMIT_FD_MAX   RLIMIT_FDMAX
#else
# ifdef RLIMIT_NOFILE
#  define RLIMIT_FD_MAX RLIMIT_NOFILE
# else
#  ifdef RLIMIT_OPEN_MAX
#   define RLIMIT_FD_MAX RLIMIT_OPEN_MAX
#  else
#   undef RLIMIT_FD_MAX
#  endif
# endif
#endif


#ifdef  REJECT_HOLD
int reject_held_fds = 0;
#endif

#ifdef NEED_SPLITCODE
extern time_t server_split_time;
extern int    server_was_split;
#endif

int cold_start = YES;   /* set if the server has just fired up */

/* /quote set variables */
struct SetOptions GlobalSetOptions;

/* config.h config file paths etc */
ConfigFileEntryType ConfigFileEntry; 

struct  Counter Count;

time_t  CurrentTime;            /* GLOBAL - current system timestamp */
int     ServerRunning;          /* GLOBAL - server execution state */
struct Client me;                     /* That's me */

struct Client* GlobalClientList = 0; /* Pointer to beginning of Client list */
/* client pointer lists -Dianora */ 
struct Client *local_cptr_list = NULL;
struct Client *oper_cptr_list  = NULL;
struct Client *serv_cptr_list  = NULL;

static size_t      initialVMTop = 0;   /* top of virtual memory at init */
static const char* logFileName = LPATH;
#if defined(__CYGWIN__)
static int         bootDaemon  = 0;
#else
static int         bootDaemon  = 1;
#endif

char**  myargv;
int     dorehash   = 0;
int     debuglevel = -1;        /* Server debug level */
char*   debugmode  = "";        /*  -"-    -"-   -"-  */


int     rehashed = YES;
int     dline_in_progress = NO; /* killing off matching D lines ? */
time_t  nextconnect = 1;        /* time for next try_connections call */
time_t  nextping = 1;           /* same as above for check_pings() */

/* code added by mika nystrom (mnystrom@mit.edu) */
/* this flag is used to signal globally that the server is heavily loaded,
   something which can be taken into account when processing e.g. user commands
   and scheduling ping checks */
/* Changed by Taner Halicioglu (taner@CERF.NET) */

#define LOADCFREQ 5     /* every 5s */
#define LOADRECV 40     /* 40k/s */

int    LRV = LOADRECV;
time_t LCF = LOADCFREQ;
float currlife = 0.0;


/*
 * get_vm_top - get the operating systems notion of the resident set size
 */
static size_t get_vm_top(void)
{
  /*
   * NOTE: sbrk is not part of the ANSI C library or the POSIX.1 standard
   * however it seems that everyone defines it. Calling sbrk with a 0
   * argument will return a pointer to the top of the process virtual
   * memory without changing the process size, so this call should be
   * reasonably safe (sbrk returns the new value for the top of memory).
   * This code relies on the notion that the address returned will be an 
   * offset from 0 (NULL), so the result of sbrk is cast to a size_t and 
   * returned. We really shouldn't be using it here but...
   */
  void* vptr = sbrk(0);
  return (size_t) vptr;
}

/*
 * get_maxrss - get the operating systems notion of the resident set size
 */
size_t get_maxrss(void)
{
  return get_vm_top() - initialVMTop;
}

/*
 * init_sys
 */
static void init_sys(int boot_daemon)
{
#ifdef RLIMIT_FD_MAX
  struct rlimit limit;

  if (!getrlimit(RLIMIT_FD_MAX, &limit))
    {

      if (limit.rlim_max < MAXCONNECTIONS)
        {
          fprintf(stderr,"ircd fd table too big\n");
          fprintf(stderr,"Hard Limit: %ld IRC max: %d\n",
                        (long) limit.rlim_max, MAXCONNECTIONS);
          fprintf(stderr,"Fix MAXCONNECTIONS\n");
          exit(-1);
        }

      limit.rlim_cur = limit.rlim_max; /* make soft limit the max */
      if (setrlimit(RLIMIT_FD_MAX, &limit) == -1)
        {
          fprintf(stderr,"error setting max fd's to %ld\n",
                        (long) limit.rlim_cur);
          exit(-1);
        }

#ifndef USE_POLL
      if( MAXCONNECTIONS > FD_SETSIZE )
        {
          fprintf(stderr, "FD_SETSIZE = %d MAXCONNECTIONS = %d\n",
                  FD_SETSIZE, MAXCONNECTIONS);
          fprintf(stderr,
            "Make sure your kernel supports a larger FD_SETSIZE then " \
            "recompile with -DFD_SETSIZE=%d\n", MAXCONNECTIONS);
          exit(-1);
        }
      printf("Value of FD_SETSIZE is %d\n", FD_SETSIZE);
#endif /* USE_POLL */
    }
#endif        /* RLIMIT_FD_MAX */

  /* This is needed to not fork if -s is on */
  if (boot_daemon)
    {
      int pid;

      if((pid = fork()) < 0)
        {
          fprintf(stderr, "Could not fork: %s\n", strerror(errno));
          exit(0);
        }
      else if (pid > 0)
        exit(0);
#ifdef TIOCNOTTY
      { /* scope */
        int fd;
        if ((fd = open("/dev/tty", O_RDWR)) >= 0)
          {
            ioctl(fd, TIOCNOTTY, NULL);
            close(fd);
          }
      }
#endif
     setsid();
    }
  close_all_connections();
}

/*
 * bad_command
 *      This is called when the commandline is not acceptable.
 *      Give error message and exit without starting anything.
 */
static void bad_command(void)
{
  fprintf(stderr, 
          "Usage: ircd [-d dlinefile] [-f configfile] [-h servername] "
          "[-k klinefile] [-l logfile] [-n] [-v] [-x debuglevel]\n");
  fprintf(stderr, "Server not started\n");
  exit(-2);
}

/*
 * All command line parameters have the syntax "-f string" or "-fstring"
 * OPTIONS:
 * -d filename - specify d:line file
 * -f filename - specify config file
 * -h hostname - specify server name
 * -k filename - specify k:line file
 * -l filename - specify log file
 * -n          - do not fork, run in foreground
 * -v          - print daemon version and exit
 * -x          - set debug level, if compiled for debug logging
 */
static void parse_command_line(int argc, char* argv[])
{
  const char* options = "d:f:h:k:l:nvx:"; 
  int         opt;

  while ((opt = getopt(argc, argv, options)) != EOF) {
    switch (opt) {
    case 'd': 
      if (optarg)
        ConfigFileEntry.dpath = optarg;
      break;
    case 'f':
#ifdef CMDLINE_CONFIG
      if (optarg)
        ConfigFileEntry.configfile = optarg;
#endif
      break;
    case 'k':
#ifdef KPATH
      if (optarg)
        ConfigFileEntry.klinefile = optarg;
#endif
      break;
    case 'h':
      if (optarg)
        strncpy_irc(me.name, optarg, HOSTLEN);
      break;
    case 'l':
      if (optarg)
        logFileName = optarg;
      break;
    case 'n':
      bootDaemon = 0; 
      break;
    case 'v':
      printf("ircd %s\n\tzlib %s\n\tircd_dir: %s\n", ircd_version,
#ifndef ZIP_LINKS
             "not used",
#else
              zlibVersion(),
#endif
              ConfigFileEntry.dpath);
      exit(0);
      break;   /* NOT REACHED */
    case 'x':
#ifdef  DEBUGMODE
      if (optarg) {
        debuglevel = atoi(optarg);
        debugmode = optarg;
      }
#endif
      break;
    default:
      bad_command();
      break;
    }
  }
}
static time_t next_gc = 0;
static time_t io_loop(time_t delay)
{
  static char   to_send[200];
  static time_t lasttime  = 0;
  static long   lastrecvK = 0;
  static int    lrv       = 0;
  time_t        lasttimeofday;
  lasttimeofday = CurrentTime;

  if (CurrentTime < lasttimeofday)
    {
      ircsprintf(to_send, "System clock is running backwards - (%d < %d)",
                 CurrentTime, lasttimeofday);
      report_error(to_send, me.name, 0);
    }
  if(!next_gc)
    {
  	next_gc = CurrentTime + 600;
    }

  /*
   * This chunk of code determines whether or not
   * "life sucks", that is to say if the traffic
   * level is so high that standard server
   * commands should be restricted
   *
   * Changed by Taner so that it tells you what's going on
   * as well as allows forced on (long LCF), etc...
   */
  
  if ((CurrentTime - lasttime) >= LCF)
    {
      lrv = LRV * LCF;
      lasttime = CurrentTime;
      currlife = (float)((long)me.receiveK - lastrecvK)/(float)LCF;
      if (((long)me.receiveK - lrv) > lastrecvK )
        {
          if (!LIFESUX)
            {
              LIFESUX = 1;

              if (NOISYHTM)
                {
                  sprintf(to_send, 
                        "Entering high-traffic mode - (%.1fk/s > %dk/s)",
                                (float)currlife, LRV);
                  sendto_ops("%s", to_send);
                }
            }
          else
            {
              LIFESUX++;                /* Ok, life really sucks! */
              LCF += 2;                 /* Wait even longer */
              if (NOISYHTM) 
                {
                  sprintf(to_send,
                        "Still high-traffic mode %d%s (%d delay): %.1fk/s",
                                LIFESUX,
                                (LIFESUX & 0x04) ?  " (TURBO)" : "",
                                (int)LCF, (float)currlife);
                  sendto_ops("%s", to_send);
                }
            }
        }
      else
        {
          LCF = LOADCFREQ;
          if (LIFESUX)
            {
              LIFESUX = 0;
              if (NOISYHTM)
                sendto_ops("Resuming standard operation...");
            }
        }
      lastrecvK = (long)me.receiveK;
    }

  /*
  ** We only want to connect if a connection is due,
  ** not every time through.  Note, if there are no
  ** active C lines, this call to Tryconnections is
  ** made once only; it will return 0. - avalon
  */
  if (nextconnect && CurrentTime >= nextconnect)
    nextconnect = try_connections(CurrentTime);

  /*
  ** take the smaller of the two 'timed' event times as
  ** the time of next event (stops us being late :) - avalon
  ** WARNING - nextconnect can return 0!
  */
  if (nextconnect)
    delay = IRCD_MIN(nextping, nextconnect);
  delay -= CurrentTime;
  /*
  ** Adjust delay to something reasonable [ad hoc values]
  ** (one might think something more clever here... --msa)
  ** We don't really need to check that often and as long
  ** as we don't delay too long, everything should be ok.
  ** waiting too long can cause things to timeout...
  ** i.e. PINGS -> a disconnection :(
  ** - avalon
  */
  if (delay < 1)
    delay = 1;
  else
    delay = IRCD_MIN(delay, TIMESEC);
  /*
   * We want to read servers on every io_loop, as well
   * as "busy" clients (which again, includes servers.
   * If "lifesux", then we read servers AGAIN, and then
   * flush any data to servers.
   *    -Taner
   */

#ifndef NO_PRIORITY
  read_message(0, FDL_SERVER);
  read_message(0, FDL_BUSY);
  if (LIFESUX)
    {
      read_message(0, FDL_SERVER);
      if (LIFESUX & 0x4)
        {       /* life really sucks */
          read_message(0, FDL_BUSY);
          read_message(0, FDL_SERVER);
        }
      flush_server_connections();
    }

  /*
   * CLIENT_SERVER = TRUE:
   *    If we're in normal mode, or if "lifesux" and a few
   *    seconds have passed, then read everything.
   * CLIENT_SERVER = FALSE:
   *    If it's been more than lifesux*2 seconds (that is, 
   *    at most 1 second, or at least 2s when lifesux is
   *    != 0) check everything.
   *    -Taner
   */
  {
    static time_t lslasttime=0;
#ifdef CLIENT_SERVER
    if (!LIFESUX || (lslasttime + LIFESUX) < CurrentTime)
      {
#else
    if ((lslasttime + (LIFESUX + 1)) < CurrentTime)
      {
#endif
        read_message(0, FDL_ALL); /*  check everything! */
        lslasttime = CurrentTime;
      }
   }
#else
  read_message(0, FDL_ALL); /*  check everything! */
  flush_server_connections();
#endif

  /*
  ** ...perhaps should not do these loops every time,
  ** but only if there is some chance of something
  ** happening (but, note that conf->hold times may
  ** be changed elsewhere--so precomputed next event
  ** time might be too far away... (similarly with
  ** ping times) --msa
  */

  if (CurrentTime >= nextping) {
    nextping = check_pings(CurrentTime);
    timeout_auth_queries(CurrentTime);
  }

  if (dorehash && !LIFESUX)
    {
      rehash(&me, &me, 1);
      dorehash = 0;
    }
  /*
  ** Flush output buffers on all connections now if they
  ** have data in them (or at least try to flush)
  ** -avalon
  */
  flush_connections(0);

#ifndef NO_PRIORITY
  fdlist_check(CurrentTime);
#endif
  
  if(CurrentTime >= next_gc)
  {
     block_garbage_collect();
     next_gc = CurrentTime + 600;
  }

  return delay;

}

/*
 * initalialize_global_set_options
 *
 * inputs       - none
 * output       - none
 * side effects - This sets all global set options needed 
 */

static void initialize_global_set_options(void)
{
  memset( &GlobalSetOptions, 0, sizeof(GlobalSetOptions));

  MAXCLIENTS = MAX_CLIENTS;
  NOISYHTM = NOISY_HTM;
  GlobalSetOptions.autoconn = 1;

#ifdef FLUD
  FLUDNUM = FLUD_NUM;
  FLUDTIME = FLUD_TIME;
  FLUDBLOCK = FLUD_BLOCK;
#endif

#ifdef IDLE_CHECK
  IDLETIME = MIN_IDLETIME;
#endif

#ifdef ANTI_SPAMBOT
  SPAMTIME = MIN_JOIN_LEAVE_TIME;
  SPAMNUM = MAX_JOIN_LEAVE_COUNT;
#endif

#ifdef ANTI_DRONE_FLOOD
  DRONETIME = DEFAULT_DRONE_TIME;
  DRONECOUNT = DEFAULT_DRONE_COUNT;
#endif

#ifdef NEED_SPLITCODE
 SPLITDELAY = (DEFAULT_SERVER_SPLIT_RECOVERY_TIME * 60);
 SPLITNUM = SPLIT_SMALLNET_SIZE;
 SPLITUSERS = SPLIT_SMALLNET_USER_SIZE;
 server_split_time = CurrentTime;
#endif

 /* End of global set options */

}

/*
 * initialize_message_files
 *
 * inputs       - none
 * output       - none
 * side effects - Set up all message files needed, motd etc.
 */

static void initialize_message_files(void)
  {
  InitMessageFile( HELP_MOTD, HPATH, &ConfigFileEntry.helpfile );
  InitMessageFile( USER_MOTD, MPATH, &ConfigFileEntry.motd );
  InitMessageFile( OPER_MOTD, OPATH, &ConfigFileEntry.opermotd );

  ReadMessageFile( &ConfigFileEntry.helpfile );
  ReadMessageFile( &ConfigFileEntry.motd );
  ReadMessageFile( &ConfigFileEntry.opermotd );
  }

/*
 * write_pidfile
 *
 * inputs       - none
 * output       - none
 * side effects - write the pid of the ircd to PPATH
 */

static void write_pidfile(void)
{
  int fd;
  char buff[20];
  if ((fd = open(PPATH, O_CREAT|O_WRONLY, 0600))>=0)
    {
      ircsprintf(buff,"%d\n", (int)getpid());
      if (write(fd, buff, strlen(buff)) == -1)
        Log("Error writing to pid file %s", PPATH);
      close(fd);
      return;
    }
  else
    Log("Error opening pid file %s", PPATH);
}

/*
 * check_pidfile
 *
 * inputs       - nothing
 * output       - nothing
 * side effects - reads pid from pidfile and checks if ircd is in process
 *                list. if it is, gracefully exits
 * -kre
 */
static void check_pidfile(void)
{
  int fd;
  char buff[20];
  pid_t pidfromfile;

  if ((fd = open(PPATH, O_RDONLY)) >= 0 )
  {
    if (read(fd, buff, sizeof(buff)) == -1)
    {
      /* printf("NOTICE: problem reading from %s (%s)\n", PPATH,
          strerror(errno)); */
    }
    else
    {
      pidfromfile = atoi(buff);
      if (pidfromfile != (int)getpid() && !kill(pidfromfile, 0))
      {
        printf("daemon is already running.\n");
        exit(-1);
      }
    }
    close(fd);
  }
  else if(errno != ENOENT)
  {
    printf("WARNING: Problem opening %s: %s\n", PPATH, strerror(errno));
  }
}

/*
 * setup_corefile
 *
 * inputs       - nothing
 * output       - nothing
 * side effects - setups corefile to system limits.
 * -kre
 */
static void setup_corefile(void)
{
#ifdef RLIMIT_CORE
  struct rlimit rlim; /* resource limits */

  /* Set corefilesize to maximum */
  if (!getrlimit(RLIMIT_CORE, &rlim))
  {
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_CORE, &rlim);
  }
#endif /* RLIMIT_CORE */
}

int main(int argc, char *argv[])
{
  time_t      delay = 0;
  aConfItem*  aconf;
  char buff[20];
  
  if(geteuid() == 0)
  {
  	fprintf(stderr, "ERROR: Do not run Ignite as root!\n");
  	return -1;
  }

  /*
   * save server boot time right away, so getrusage works correctly
   */
  if ((CurrentTime = time(0)) == -1)
    {
      fprintf(stderr, "ERROR: Clock failure: %s\n", strerror(errno));
      exit(errno);
    }

  /*
   * Setup corefile size immediately after boot
   */
  setup_corefile();

  /* 
   * set initialVMTop before we allocate any memory
   */
  initialVMTop = get_vm_top();

  /*
   * Initialize the Blockheap allocator
   */
  initBlockHeap();

  ServerRunning = 0;
  memset(&me, 0, sizeof(me));
  GlobalClientList = &me;       /* Pointer to beginning of Client list */
  cold_start = YES;             /* set when server first starts up */

  memset(&Count, 0, sizeof(Count));
  Count.server = 1;     /* us */

  initialize_global_set_options();

#ifdef REJECT_HOLD
  reject_held_fds = 0;
#endif

  ConfigFileEntry.dpath = DPATH;

  ConfigFileEntry.configfile = CPATH;   /* Server configuration file */

#ifdef KPATH
  ConfigFileEntry.klinefile = KPATH;         /* Server kline file */
#else
  ConfigFileEntry.klinefile = CPATH;
#endif /* KPATH */

#ifdef DLPATH
  ConfigFileEntry.dlinefile = DLPATH;
#else
  ConfigFileEntry.dlinefile = CPATH;
#endif /* DLPATH */

#ifdef GLINES
  ConfigFileEntry.glinefile = GLINEFILE;
#endif

#ifdef  ZIP_LINKS
  /* Make sure the include files match the library version number. */
  /* configure takes care of looking for zlib and zlibVersion(). */
  if (strcmp(zlibVersion(), ZLIB_VERSION) != 0)
  {
    fprintf(stderr, "WARNING: zlib include files differ from library.\n");
    fprintf(stderr, "WARNING: ZIPLINKS may fail!\n");
    fprintf(stderr, "WARNING: library %s, include files %s\n",
            zlibVersion(), ZLIB_VERSION);
  }
#endif

  myargv = argv;
  umask(077);                /* better safe than sorry --SRB */

  parse_command_line(argc, argv); 

  if (chdir(ConfigFileEntry.dpath))
    {
      perror("chdir");
      exit(-1);
    }

  /*
   * Check if daemon is already running
   */

  /* We need to start logging first so we can capture all the startup messages -malign */
  init_log(logFileName);

  /* Some informational stuff logged on Ignite startup -malign */
  Log("%s starting...", VERSION);
  Log("http://www.necrotic.net/ignite/");
  Log("This software is bound by the terms of the GNU GPL.");

  Log("Checking for pidfile...");
  check_pidfile();
  Log("Pidfile OK.");

  Log("Daemonizing...");
  init_sys(bootDaemon);
  Log("Daemonized.");

  /* Necessary to reinit logging because once fork() and setsid() are called, we lose the parent file descriptor -malign */
  Log("Initializing logging...");
  init_log(logFileName);
  Log("Logging initialized.");

  Log("Setting up signal handling...");
  setup_signals();
  Log("Signal handling setup complete.");

  Log("Caching message files..."); 
  initialize_message_files();
  Log("Message files cached.");

  Log("Creating ISupport string... ");
  isupport = make_isupport();
  Log("ISupport string created.");

  Log("Allocating memory...");
  dbuf_init();  /* set up some dbuf stuff to control paging */
  Log("Memory allocated.");

  Log("Initializing hash table...");
  init_hash();

  clear_scache_hash_table();    /* server cache name table */
  clear_ip_hash_table();        /* client host ip hash table */
  clear_Dline_table();          /* d line tree */
  initlists();
  initclass();
  initwhowas();
  init_stats();
  init_tree_parse(msgtab);      /* tree parse code (orabidoo) */
  Log("Hash table initialized.");

  Log("Setting up file descriptors...");
  fdlist_init();
  Log("File descriptor setup complete.");

  Log("Initializing network...");
  init_netio();
  Log("Network initialized.");

  Log("Parsing configuration file...");
  read_conf_files(YES);         /* cold start init conf files */

  aconf = find_me();
  if (EmptyString(me.name))
    strncpy_irc(me.name, aconf->host, HOSTLEN);
  strncpy_irc(me.host, aconf->host, HOSTLEN);

  me.fd = -1;
  me.from = &me;
  me.servptr = &me;
  SetMe(&me);
  make_server(&me);

  me.serv->up = me.name;
  me.lasttime = me.since = me.firsttime = CurrentTime;
  add_to_client_hash_table(me.name, &me);
  
  check_class();
  Log("Configuration parsed OK.");

  Log("Writing pid to pidfile...");
  write_pidfile();
  Log("PID written to pidfile.");

  ircsprintf(buff,"%d", (int)getpid());
  Log("Process ID: %s", buff);
  Log("Resident Set Size: %d bytes", get_maxrss());

  Log("Server startup complete.");

  ServerRunning = 1;
  while (ServerRunning) {
    usleep(100000);
    do_adns_io();
    delay = io_loop(delay);
    do_adns_io();
  }
  return 0;
}
