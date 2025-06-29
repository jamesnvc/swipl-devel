/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2011-2023, University of Amsterdam
			      VU University Amsterdam
			      CWI, Amsterdam
			      SWI-Prolog Solutions b.v.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

/*  Modified (M) 1993 Dave Sherratt  */

/*#define O_DEBUG 1*/

#if OS2 && EMX
#include <os2.h>                /* this has to appear before pl-incl.h */
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Solaris has asctime_r() with 3 arguments. Using _POSIX_PTHREAD_SEMANTICS
is supposed to give the POSIX standard one.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(__sun__) || defined(__sun)
#define _POSIX_PTHREAD_SEMANTICS 1
#endif

#define __MINGW_USE_VC2005_COMPAT	/* Get Windows time_t as 64-bit */

#ifdef __WINDOWS__
#include <winsock2.h>
#include <sys/stat.h>
#include <windows.h>

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

#include "pl-incl.h"
#include "pl-ctype.h"
#include "pl-utf8.h"
#include "../pl-fli.h"
#include "../pl-setup.h"
#include <math.h>
#include <stdio.h>		/* rename() and remove() prototypes */

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef O_XOS
#define statstruct struct _stati64
#else
#define statstruct struct stat
#define statfunc stat
#endif
#if HAVE_PWD_H
#include <pwd.h>
#endif
#if HAVE_VFORK_H
#include <vfork.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#if defined(HAVE_SYS_RESOURCE_H)
#include <sys/resource.h>
#endif
#ifdef HAVE_FTIME
#include <sys/timeb.h>
#endif
#include <time.h>
#include <fcntl.h>
#ifndef __WATCOMC__			/* appears a conflict */
#include <errno.h>
#endif

#if defined(__WATCOMC__)
#include <io.h>
#include <dos.h>
#endif

#if OS2 && EMX
static double initial_time;
#endif /* OS2 */

static void	initExpand(void);
static void	cleanupExpand(void);
static void	initEnviron(void);
static char    *utf8_path_lwr(char *s, size_t len);
static void	clean_tmp_dir(void);

#ifndef DEFAULT_PATH
#define DEFAULT_PATH "/bin:/usr/bin"
#endif

#if defined(HAVE_CRT_EXTERNS_H) && defined(HAVE__NSGETENVIRON)
/* MacOS */
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif

		/********************************
		*         INITIALISATION        *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    bool initOs()

    Initialise the OS dependant functions.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

bool
initOs(void)
{ GET_LD

  DEBUG(1, Sdprintf("OS:initExpand() ...\n"));
  initExpand();
  DEBUG(1, Sdprintf("OS:initEnviron() ...\n"));
  initEnviron();

#ifdef __WINDOWS__
  setPrologFlagMask(PLFLAG_FILE_CASE_PRESERVING);
#else
  setPrologFlagMask(PLFLAG_FILE_CASE);
  setPrologFlagMask(PLFLAG_FILE_CASE_PRESERVING);
#endif

  DEBUG(1, Sdprintf("OS:done\n"));

  succeed;
}


void
cleanupOs(void)
{ cleanupExpand();
  clean_tmp_dir();
}


		/********************************
		*            OS ERRORS          *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    char *OsError()
	Return a char *, holding a description of the last OS call error.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

char *
OsError(void)
{
#ifdef HAVE_STRERROR
#ifdef __WINDOWS__
  return strerror(_xos_errno());
#else
  return strerror(errno);
#endif
#else /*HAVE_STRERROR*/
static char errmsg[64];

#ifdef __unix__
  extern int sys_nerr;
#if !EMX
  extern char *sys_errlist[];
#endif
  extern int errno;

  if ( errno < sys_nerr )
    return sys_errlist[errno];
#endif

  Ssprintf(errmsg, "Unknown Error (%d)", errno);
  return errmsg;
#endif /*HAVE_STRERROR*/
}

		/********************************
		*    PROCESS CHARACTERISTICS    *
		*********************************/

#ifdef O_MITIGATE_SPECTRE
static inline double
clock_jitter(double t)
{ GET_LD

  if ( unlikely(truePrologFlag(PLFLAG_MITIGATE_SPECTRE)) )
  { double i;

    modf(t*50000.0, &i);
    t = i/50000.0;
  }

  return t;
}
#else
#define clock_jitter(t) (t)
#endif

#ifdef HAVE_CLOCK_GETTIME
#define timespec_to_double(ts) \
	((double)(ts).tv_sec + (double)(ts).tv_nsec/(double)1000000000.0)
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    double CpuTime(cputime_kind)

    Returns a floating point number, representing the amount  of  (user)
    CPU-seconds  used  by the process Prolog is in.  For systems that do
    not allow you to obtain this information  you  may  wish  to  return
    elapsed  time  since Prolog was started, as this function is used to
    by consult/1 and time/1 to determine the amount of CPU time used  to
    consult a file or to execute a query.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef __WINDOWS__			/* defined in pl-nt.c */

#ifdef HAVE_TIMES
#include <sys/times.h>

#if defined(_SC_CLK_TCK)
#define Hz ((int)sysconf(_SC_CLK_TCK))
#else
#ifdef HZ
#  define Hz HZ
#else
#  define Hz 60				/* if nothing better: guess */
#endif
#endif /*_SC_CLK_TCK*/
#endif /*HAVE_TIMES*/

double
CpuTime(cputime_kind which)
{
#ifndef __EMSCRIPTEN__
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_PROCESS_CPUTIME_ID)
#define CPU_TIME_DONE
  struct timespec ts;
  (void)which;

  if ( clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0 )
    return clock_jitter(timespec_to_double(ts));
  return 0.0;
#endif

#if !defined(CPU_TIME_DONE) && defined(HAVE_TIMES)
#define CPU_TIME_DONE
  struct tms t;
  double used;
  static int MTOK_got_hz = false;
  static double MTOK_hz;

  if ( !MTOK_got_hz )
  { MTOK_hz = (double) Hz;
    MTOK_got_hz++;
  }
  times(&t);

  switch( which )
  { case CPU_USER:
      used = (double) t.tms_utime / MTOK_hz;
      break;
    case CPU_SYSTEM:
    default:				/* make compiler happy */
      used = (double) t.tms_stime / MTOK_hz;
  }

  if ( isnan(used) )			/* very dubious, but this */
    used = 0.0;				/* happens when running under GDB */

  return clock_jitter(used);
#endif
#endif /* not __EMSCRIPTEN__ */

#if !defined(CPU_TIME_DONE)
  GET_LD
  (void)which;

  return WallTime() - LD->statistics.start_time;

  return 0.0;
#endif
}

#endif /*__WINDOWS__*/


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
clock_gettime() is provided by MinGW32,  but   where  time_t is 64 bits,
only a 32-bit value is currectly   filled, making get_time/1 return very
large bogus values. Ideally this should have   a  runtime check. For now
we'll  hope  that  32-bit  Windows  is   extinct  before  32-bit  time_t
overflows.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef WIN32
#undef HAVE_CLOCK_GETTIME
#endif

double
WallTime(void)
{ double stime;

#if HAVE_CLOCK_GETTIME
  struct timespec tp;

  clock_gettime(CLOCK_REALTIME, &tp);
  stime = timespec_to_double(tp);
#else
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tp;

  gettimeofday(&tp, NULL);
  stime = (double)tp.tv_sec + (double)tp.tv_usec/1000000.0;
#else
#ifdef HAVE_FTIME
  struct timeb tb;

  ftime(&tb);
  stime = (double)tb.time + (double)tb.millitm/1000.0;
#else
  stime = (double)time((time_t *)NULL);
#endif
#endif
#endif

  return clock_jitter(stime);
}

		 /*******************************
		 *	      FEATURES		*
		 *******************************/

#ifndef __WINDOWS__			/* Windows version in pl-nt.c */

#ifdef HAVE_SC_NPROCESSORS_CONF

int
CpuCount(void)
{ return sysconf(_SC_NPROCESSORS_CONF);
}

#else

#ifdef PROCFS_CPUINFO
int
CpuCount(void)
{ FILE *fd = fopen("/proc/cpuinfo", "r");

  if ( fd )
  { char buf[256];
    int count = 0;

    while(fgets(buf, sizeof(buf)-1, fd))
    { char *vp;

      if ( (vp = strchr(buf, ':')) )
      { char *en;

	for(en=vp; en > buf && en[-1] <= ' '; en--)
	  ;
	*en = EOS;
	DEBUG(2, Sdprintf("Got %s = %s\n", buf, vp+2));
	if ( streq("processor", buf) && isDigit(vp[2]) )
	{ int cpu = atoi(vp+2);

	  if ( cpu+1 > count )
	    count = cpu+1;
	}
      }
    }

    fclose(fd);
    return count;
  }

  return 0;
}

#else /*PROCFS_CPUINFO*/

#ifdef HAVE_SYSCTLBYNAME	/* MacOS X */

#include <sys/param.h>
#include <sys/sysctl.h>

int
CpuCount(void)
{ int     count ;
  size_t  size=sizeof(count) ;

  if ( sysctlbyname("hw.ncpu", &count, &size, NULL, 0) )
    return 0;

  return count;
}

#else

int
CpuCount(void)
{ return 0;
}

#endif /*sysctlbyname*/

#endif /*PROCFS_CPUINFO*/

#endif /*HAVE_SC_NPROCESSORS_CONF*/


void
setOSPrologFlags(void)
{ int cpu_count = CpuCount();

  if ( cpu_count > 0 )
    PL_set_prolog_flag("cpu_count", PL_INTEGER, (intptr_t)cpu_count);
}
#endif

		 /*******************************
		 *	       MEMORY		*
		 *******************************/

uintptr_t
UsedMemory(void)
{
#if defined(HAVE_GETRUSAGE) && defined(HAVE_RU_IDRSS)
  struct rusage usage;

  if ( getrusage(RUSAGE_SELF, &usage) == 0 &&
       usage.ru_idrss )
  { return usage.ru_idrss;		/* total unshared data */
  }
#endif

  return heapUsed();			/* from pl-alloc.c */
}


uintptr_t
FreeMemory(void)
{
#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_DATA)
  uintptr_t used = UsedMemory();
  struct rlimit limit;

  if ( getrlimit(RLIMIT_DATA, &limit) == 0 )
  { if ( limit.rlim_cur == RLIM_INFINITY )
      return (uintptr_t)-1;
    else
      return (uintptr_t)(limit.rlim_cur - used);
  }
#endif

  return 0L;
}


		/********************************
		*           ARITHMETIC          *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    uint64_t _PL_Random()

    Return a random number. Used for arithmetic only. More trouble. On
    some systems (__WINDOWS__) the seed of rand() is thread-local, while on
    others it is global.  We appear to have the choice between

	# srand()/rand()
	Differ in MT handling, often bad distribution

	# srandom()/random()
	Not portable, not MT-Safe but much better distribution

	# drand48() and friends
	Depreciated according to Linux manpage, suggested by Solaris
	manpage.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void
setRandom(unsigned int *seedp)
{ unsigned int seed;

  if ( seedp )
  { seed = *seedp;
  } else
  {
#ifdef __WINDOWS__
     seed = (unsigned int)GetTickCount();
#else
#ifdef HAVE_GETTIMEOFDAY
     struct timeval tp;

     gettimeofday(&tp, NULL);
     seed = (unsigned int)(tp.tv_sec + tp.tv_usec);
#else
     seed = (unsigned int)time((time_t *) NULL);
#endif
#endif
  }
  seed += PL_thread_self();

#ifdef HAVE_SRANDOM
  srandom(seed);
#else
#ifdef HAVE_SRAND
  srand(seed);
#endif
#endif
}

uint64_t
_PL_Random(void)
{ GET_LD

  if ( !LD->os.rand_initialised )
  { setRandom(NULL);
    LD->os.rand_initialised = true;
  }

#ifdef HAVE_RANDOM
  { uint64_t l = random();

    l ^= (uint64_t)random()<<15;
    l ^= (uint64_t)random()<<30;
    l ^= (uint64_t)random()<<45;

    return l;
  }
#else
  { uint64_t l = rand();			/* 0<n<2^15-1 */

    l ^= (uint64_t)rand()<<15;
    l ^= (uint64_t)rand()<<30;
    l ^= (uint64_t)rand()<<45;

    return l;
  }
#endif
}

		/********************************
		*             FILES             *
		*********************************/

      /* (Everything you always wanted to know about files ...) */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Generation and administration of temporary files.  Currently  only  used
by  the foreign language linker.  It might be useful to make a predicate
available to the Prolog user based on these functions.  These  functions
are  in  this  module as non-UNIX OS probably don't have getpid() or put
temporaries on /tmp.

    atom_t TemporaryFile(const char *id, const char *ext, int *fdp)

    The return value of this call is an atom,  whose  string  represents
    the  path  name of a unique file that can be used as temporary file.
    `id' is a char * that can be used to make it easier to identify  the
    file as a specific kind of SWI-Prolog intermediate file.  `ext`
    provides the optional extension.

    void RemoveTemporaryFiles()

    Remove all temporary files.  This function should be  aware  of  the
    fact  that some of the file names generated by TemporaryFile() might
    not be created at all, or might already have been deleted.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
free_tmp_name(atom_t tname)
{ int rc;
  PL_chars_t txt;

  get_atom_text(tname, &txt);
  PL_mb_text(&txt, REP_FN);
  rc = RemoveFile(txt.text.t);
  PL_free_text(&txt);

  PL_unregister_atom(tname);
  return rc;
}


static void
free_tmp_symbol(table_key_t name, table_value_t value)
{ (void)value;
  (void)free_tmp_name((atom_t)name);
}


#ifndef O_EXCL
#define O_EXCL 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef SWIPL_TMP_DIR
#define SWIPL_TMP_DIR "/tmp"
#endif

/* tmp_dir() returns the temporary file directory in REP_FN
 * encoding.
 */

static atom_t      tmp_aname = NULL_ATOM;
static const char *tmp_name = NULL;

static void
clean_tmp_dir(void)
{ if ( tmp_name )
  { PL_free((void*)tmp_name);
    tmp_name = NULL;
  }
  if ( tmp_aname )
  { PL_unregister_atom(tmp_aname);
    tmp_aname = 0;
  }
}

static const char *
tmp_dir(void)
{ GET_LD

#ifdef O_PLMT
  if ( LD )
#endif
  { atom_t a;

    if ( PL_current_prolog_flag(ATOM_tmp_dir, PL_ATOM, &a) )
    { if ( a == tmp_aname )
      { return tmp_name;
      } else
      { term_t t;
	char *s;

	if ( (t=PL_new_term_ref()) &&
	     PL_put_atom(t, a) &&
	     PL_get_chars(t, &s, CVT_ATOM|REP_FN|BUF_MALLOC) )
	{ clean_tmp_dir();

	  tmp_aname = a;
	  tmp_name = s;
	  PL_register_atom(tmp_aname);

	  return tmp_name;
	}
      }
    }
  }

  return SWIPL_TMP_DIR;
}


static int
verify_tmp_dir(const char* tmpdir)
{ const char *reason = NULL;

  if ( tmpdir == NULL )
    return false;

  if ( !ExistsDirectory(tmpdir) )
    reason = "no such directory";

  if ( reason )
  { if ( printMessage(ATOM_warning,
		      PL_FUNCTOR_CHARS, "invalid_tmp_dir", 2,
		      PL_CHARS, tmpdir,
		      PL_CHARS, reason) )
    { /* to prevent ignoring return value warning */ }
    return false;
  }

  return true;
}

#ifdef O_XOS
static wchar_t *
xos_plain_name(const char *from, wchar_t *buf, size_t len)
{ wchar_t *rc = _xos_os_filenameW(from, buf, len);

  if ( rc )
    rc += _xos_win_prefix_length(rc);

  return rc;
}
#endif

atom_t
TemporaryFile(const char *id, const char *ext, int *fdp)
{ char temp[PATH_MAX];
  const char *tmpdir = NULL;
  atom_t tname;
  static int temp_counter = 0;

  tmpdir = tmp_dir();

  if ( !verify_tmp_dir(tmpdir) )
    return NULL_ATOM;

retry:
{ int tmpid = ATOMIC_INC(&temp_counter);

#ifdef O_XOS
#define SAFE_PATH_MAX 260
  char *tmp;
  int rc;
  wchar_t *wtmp = NULL, *wtmpdir, *wid;
  wchar_t buf1[SAFE_PATH_MAX], buf2[SAFE_PATH_MAX];

  rc = ( (wtmpdir = xos_plain_name(tmpdir, buf1, SAFE_PATH_MAX)) &&
	 (wid     = _xos_utf8towcs(buf2,     id, SAFE_PATH_MAX)) &&
	 (wtmp    = _wtempnam(wtmpdir, wid)) &&
	 (tmp     = _xos_canonical_filenameW(wtmp, temp, sizeof(temp), 0)) );
  if ( wtmp )
    free(wtmp);

  if ( rc )
  { if ( !PrologPath(tmp, temp, sizeof(temp)) )
      return NULL_ATOM;
  } else
  { const char *sep  = id[0] ? "_" : "";
    const char *esep = ext[0] ? "." : "";

    if ( Ssnprintf(temp, sizeof(temp), "%s/swipl_%s%s%d%s%s",
		   tmpdir, id, sep, tmpid, esep, ext) < 0 )
    { errno = ENAMETOOLONG;
      return NULL_ATOM;
    }
  }
#else
  const char *sep  = id[0]  ? "_" : "";
  const char *esep = ext[0] ? "." : "";

  if ( Ssnprintf(temp, sizeof(temp), "%s/swipl_%s%s%d_%d%s%s",
		 tmpdir, id, sep, (int) getpid(),
		 tmpid,
		 esep, ext) < 0 )
  { errno = ENAMETOOLONG;
    return NULL_ATOM;
  }
#endif
}

  if ( fdp )
  { int fd;

    if ( (fd=open(temp, O_CREAT|O_EXCL|O_WRONLY|O_BINARY, 0600)) < 0 )
    { if ( errno == EEXIST )
	goto retry;

      return NULL_ATOM;
    }

    *fdp = fd;
  }

  tname = PL_new_atom_mbchars(REP_FN, (size_t)-1, temp); /* locked: ok! */

  if ( !GD->os.tmp_files )
  { PL_LOCK(L_OS);
    if ( !GD->os.tmp_files )
    { Table ht = newHTable(4);
      ht->free_symbol = free_tmp_symbol;
      GD->os.tmp_files = ht;
    }
    PL_UNLOCK(L_OS);
  }

  addNewHTable(GD->os.tmp_files, (table_key_t)tname, true);

  return tname;
}


int
DeleteTemporaryFile(atom_t name)
{ GET_LD
  int rc = false;

  if ( GD->os.tmp_files )
  { if ( GD->os.tmp_files )
    { if ( deleteHTable(GD->os.tmp_files, (table_key_t)name) )
	rc = free_tmp_name(name);
    }
  }

  return rc;
}


void
RemoveTemporaryFiles(void)
{ PL_LOCK(L_OS);
  if ( GD->os.tmp_files )
  { Table t = GD->os.tmp_files;

    GD->os.tmp_files = NULL;
    PL_UNLOCK(L_OS);
    destroyHTable(t);
  } else
  { PL_UNLOCK(L_OS);
  }
}


#if O_HPFS

/*  Conversion rules Prolog <-> OS/2 (using HPFS)
    / <-> \
    /x:/ <-> x:\  (embedded drive letter)
    No length restrictions up to PATH_MAX, no case conversions.
*/

char *
PrologPath(char *ospath, char *path, size_t len)
{ char *s = ospath, *p = path;
  int limit = len-1;

  if (isLetter(s[0]) && s[1] == ':')
  { *p++ = '/';
    *p++ = *s++;
    *p++ = *s++;
    limit -= 3;
  }
  for(; *s && limit; s++, p++, limit--)
    *p = (*s == '\\' ? '/' : makeLower(*s));

  if ( limit )
  { *p = EOS;
    return path;
  } else
  { path[0] = EOS;
    errno = ENAMETOOLONG;
    return NULL;
  }
}


char *
OsPath(const char *plpath, char *path)
{ const char *s = plpath, *p = path;
  int limit = PATH_MAX-1;

  if ( s[0] == '/' && isLetter(s[1]) && s[2] == ':') /* embedded drive letter*/
  { s++;
    *p++ = *s++;
    *p++ = *s++;
    if ( *s != '/' )
      *p++ = '\\';
    limit -= 2;
  }

  for(; *s && limit; s++, p++, limit--)
    *p = (*s == '/' ? '\\' : *s);
  if ( p[-1] == '\\' && p > path )
    p--;
  *p = EOS;

  return path;
}
#endif /* O_HPFS */

#ifdef __unix__
char *
PrologPath(const char *p, char *buf, size_t len)
{ if ( strlen(p) < len )
    return strcpy(buf, p);

  *buf = EOS;
  errno = ENAMETOOLONG;
  return NULL;
}

char *
OsPath(const char *p, char *buf)
{ strcpy(buf, p);

  return buf;
}
#endif /*__unix__*/

#if O_XOS
char *
PrologPath(const char *p, char *buf, size_t len)
{ if ( _xos_canonical_filename(p, buf, len, 0) == buf )
  { GET_LD

    if ( truePrologFlag(PLFLAG_FILE_CASE) )
    { if ( !utf8_path_lwr(buf, len) )
	return NULL;
      Sdprintf("Now %s\n", buf);
    }

    return buf;
  }

  return NULL;
}

char *
OsPath(const char *p, char *buf)
{ strcpy(buf, p);

  return buf;
}
#endif /* O_XOS */


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    char *AbsoluteFile(const char *file, char *path)

    Expand a file specification to a system-wide unique  description  of
    the  file  that can be passed to the file functions that take a path
    as argument.  Path should refer to the same file, regardless of  the
    current  working  directory.   On  Unix absolute file names are used
    for this purpose.

    This  function  is  based  on  a  similar  (primitive)  function  in
    Edinburgh C-Prolog.

    char *BaseName(path, char *base)
	 char *path;

    Return the basic file name for a file having path `path'.

    char *DirName(const char *path, char *dir)

    Return the directory name for a file having path `path'.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(HAVE_SYMLINKS) && (defined(HAVE_STAT) || defined(__unix__))
#define O_CANONICALISE_DIRS

struct canonical_dir
{ char *	name;			/* name of directory */
  char *	canonical;		/* canonical name of directory */
  dev_t		device;			/* device number */
  ino_t		inode;			/* inode number */
  CanonicalDir  next;			/* next in chain */
};

#define canonical_dirlist (GD->os._canonical_dirlist)

forwards char   *canonicaliseDir(char *);
#endif /*O_CANONICALISE_DIRS*/

static void
initExpand(void)
{
#ifdef O_CANONICALISE_DIRS
  char *dir;
  char *cpaths;
#endif

  GD->paths.CWDdir = NULL;
  GD->paths.CWDlen = 0;

#ifdef O_CANONICALISE_DIRS
{ char envbuf[PATH_MAX];

  if ( (cpaths = Getenv("CANONICAL_PATHS", envbuf, sizeof(envbuf))) )
  { char buf[PATH_MAX];

    while(*cpaths)
    { char *e;

      if ( (e = strchr(cpaths, ':')) )
      { int l = e-cpaths;

	strncpy(buf, cpaths, l);
	buf[l] = EOS;
	cpaths += l+1;
	canonicaliseDir(buf);
      } else
      { canonicaliseDir(cpaths);
	break;
      }
    }
  }

  if ( (dir = Getenv("HOME", envbuf, sizeof(envbuf))) ) canonicaliseDir(dir);
  if ( (dir = Getenv("PWD",  envbuf, sizeof(envbuf))) ) canonicaliseDir(dir);
  if ( (dir = Getenv("CWD",  envbuf, sizeof(envbuf))) ) canonicaliseDir(dir);
}
#endif
}

#ifdef O_CANONICALISE_DIRS
#define OS_DIR_TABLE_SIZE 32

static unsigned int
dir_key(const char *name, unsigned int size)
{ unsigned int k = MurmurHashAligned2(name, strlen(name), MURMUR_SEED);

  return k & (size-1);
}

static CanonicalDir
lookupCanonicalDir(const char *name)
{ if ( GD->os.dir_table.size )
  { CanonicalDir cd;
    unsigned int k = dir_key(name, GD->os.dir_table.size);

    for(cd = GD->os.dir_table.entries[k]; cd; cd = cd->next)
    { if ( streq(cd->name, name) )
	return cd;
    }
  }

  return NULL;
}


static CanonicalDir
lookupCanonicalDirFromId(const statstruct *buf)
{ if ( GD->os.dir_table.size )
  { unsigned i;

    for(i=0; i<GD->os.dir_table.size; i++)
    { CanonicalDir dn = GD->os.dir_table.entries[i];

      for( ; dn; dn = dn->next )
      { if ( dn->inode  == buf->st_ino &&
	     dn->device == buf->st_dev )
	  return dn;
      }
    }
  }

  return NULL;
}


static CanonicalDir
createCanonicalDir(const char *name, const char *canonical, const statstruct *buf)
{ CanonicalDir cd;

  if ( !GD->os.dir_table.entries )
  { size_t bytes = sizeof(*GD->os.dir_table.entries)*OS_DIR_TABLE_SIZE;

    GD->os.dir_table.entries = PL_malloc(bytes);
    memset(GD->os.dir_table.entries, 0, bytes);
    GD->os.dir_table.size = OS_DIR_TABLE_SIZE;
  }

  unsigned int k = dir_key(name, GD->os.dir_table.size);
  cd = PL_malloc(sizeof(*cd));
  cd->name      = store_string(name);
  cd->canonical = name == canonical ? cd->name : store_string(canonical);
  cd->device    = buf->st_dev;
  cd->inode     = buf->st_ino;

  cd->next = GD->os.dir_table.entries[k];
  GD->os.dir_table.entries[k] = cd;

  return cd;
}


static void
deleteCanonicalDir(CanonicalDir d)
{ unsigned int k = dir_key(d->name, GD->os.dir_table.size);

  if ( d == GD->os.dir_table.entries[k] )
  { GD->os.dir_table.entries[k] = d->next;
  } else
  { CanonicalDir cd;

    for(cd=GD->os.dir_table.entries[k]; cd; cd=cd->next)
    { if ( cd->next == d )
      { cd->next = d->next;
	break;
      }
    }

    assert(cd);
  }

  remove_string(d->name);
  if ( d->canonical != d->name )
    remove_string(d->canonical);
  PL_free(d);
}


static void
cleanupExpand(void)
{ if ( GD->os.dir_table.size )
  { unsigned i;

    for(i=0; i<GD->os.dir_table.size; i++)
    { CanonicalDir dn = GD->os.dir_table.entries[i];
      CanonicalDir next;

      for( ; dn; dn = next )
      { next = dn->next;
	if ( dn->canonical && dn->canonical != dn->name )
	  remove_string(dn->canonical);
	remove_string(dn->name);
	PL_free(dn);
      }
    }

    GD->os.dir_table.size = 0;
    PL_free(GD->os.dir_table.entries);
  }

  PL_changed_cwd();
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
verify_entry() verifies the path cache for this   path is still safe. If
not it updates the cache and returns false.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
verify_entry(CanonicalDir d)
{ char tmp[PATH_MAX];
  statstruct buf;

  if ( statfunc(OsPath(d->canonical, tmp), &buf) == 0 )
  { if ( d->inode  == buf.st_ino &&
	 d->device == buf.st_dev )
      return true;

    DEBUG(MSG_OS_DIR, Sdprintf("%s: inode/device changed\n", d->canonical));

    d->inode  = buf.st_ino;
    d->device = buf.st_dev;
    return true;
  } else
  { DEBUG(MSG_OS_DIR, Sdprintf("%s: no longer exists\n", d->canonical));

    deleteCanonicalDir(d);
  }

  return false;
}


static char *
canonicaliseDir_sync(char *path)
{ CanonicalDir d;
  statstruct buf;
  char tmp[PATH_MAX];

  DEBUG(MSG_OS_DIR, Sdprintf("canonicaliseDir(%s) --> ", path));

  if ( (d=lookupCanonicalDir(path)) && verify_entry(d) )
  { if ( d->name != d->canonical )
      strcpy(path, d->canonical);

    DEBUG(MSG_OS_DIR, Sdprintf("(lookup ino=%ld) %s\n", (long)d->inode, path));
    return path;
  }

  if ( statfunc(OsPath(path, tmp), &buf) == 0 )
  { char parent[PATH_MAX];
    char *e = path + strlen(path);

    DEBUG(MSG_OS_DIR, Sdprintf("Looking for ino=%ld\n", buf.st_ino));
    if ( (d=lookupCanonicalDirFromId(&buf)) &&
	 verify_entry(d) )
    { DEBUG(MSG_OS_DIR, Sdprintf("(found by id)\n"));
      strcpy(path, d->canonical);
      return path;
    }

    for(e--; *e != '/' && e > path + 1; e-- )
      ;
    if ( e > path )
    { strncpy(parent, path, e-path);
      parent[e-path] = EOS;

      canonicaliseDir_sync(parent);
      strcpy(parent+strlen(parent), e);

      createCanonicalDir(path, parent, &buf);
      strcpy(path, parent);
      DEBUG(MSG_OS_DIR, Sdprintf("(new ino=%ld) %s\n", (long)buf.st_ino, path));
      return path;
    } else
    { createCanonicalDir(path, path, &buf);
      return path;
    }
  }

  DEBUG(MSG_OS_DIR, Sdprintf("(nonexisting) %s\n", path));
  return path;
}

static char *
canonicaliseDir(char *path)
{ char *s;

  PL_LOCK(L_OSDIR);
  s = canonicaliseDir_sync(path);
  PL_UNLOCK(L_OSDIR);

  return s;
}

#else

#define canonicaliseDir(d)

static void
cleanupExpand(void)
{
}

#endif /*O_CANONICALISE_DIRS*/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Skip //NetBIOSName/, returning a pointer to the   final / in the pattern
if NetBIOSName is a valid NetBIOS  name.   A  valid  NetBIOS name is any
sequence of 16 8-bit characters that doesn't start with a '*'.  When used
as a file name there are additional name limitations:

https://support.microsoft.com/en-us/help/909264/naming-conventions-in-active-directory-for-computers-domains-sites-and

Note that NetBIOS names are case sensitive!

We disallow '.' in NetBIOS  names  as   well.  These  are not allowed in
recent Windows anyway. By disallowing '.'  we can distinguish host names
and thus disambiguate case insensitive host   names  from case sensitive
NetBIOS names.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef O_NETBIOS
static int
isNetBIOSChar(int c)
{ return (c && !(c == '\\' || c == '/' || c == '*' || c == '?' ||
		 c == '<'  || c == '>' || c == '|' || c == '.'));
}

static char *
skipNetBIOSName(const char *s)
{ if ( s[0] == '/' && s[1] == '/' && isNetBIOSChar(s[2]) )
  { int i;

    for(i=3; i<2+16 && isNetBIOSChar(s[i]); i++)
      ;
    if ( i > 2+16 )
      return NULL;
    if ( s[i] == '/' )
      return (char*)&s[i];
  }

  return NULL;
}
#endif

/* Remove redundant /, ./, x/../, etc.   Note that this normally makes
   the path shorter, except for Windows c:name/..., which must become
   c:/name/...
*/

char *
canonicaliseFileName(char *path, size_t buflen)
{ char *out = path, *in = path, *start = path;
  tmp_buffer saveb;
  int sl;

#ifdef O_HASDRIVES			/* C: */
  if ( in[1] == ':' && isLetter(in[0]) )
  { in += 2;

    if ( in[0] != '/' )
    { size_t len = strlen(in);
      if ( len+4 > buflen )		/* 2+"/"+0 */
	return PL_representation_error("max_path_length"),NULL;

      memmove(in+1, in, len+1);
      in[0] = '/';
    }

    out = start = in;
  }
#ifdef __MINGW32__ /* /c/ in MINGW is the same as c: */
  else if ( in[0] == '/' && isLetter(in[1]) &&
	    in[2] == '/' )
  {
    out[0] = in[1];
    out[1] = ':';
    in += 3;
    out = start = in;
  }
#endif
#endif

  if ( (sl=file_name_is_iri(in)) )
  { in += (sl+3);
    out = start = in;
  }

#if defined(O_NETBIOS) || defined(O_HASSHARES)
  if ( in[0] == '/' && in[1] == '/' )
  { char *s = NULL;

#ifdef O_NETBIOS
    s = skipNetBIOSName(in);
#endif

#ifdef O_HASSHARES			/* //host/ */
    if ( s == NULL && isAlpha(in[2]) )
    { for(s = in+3; *s && (isAlpha(*s) || *s == '-' || *s == '.'); s++)
	;
    }
#endif

    if ( s && *s == '/' )
    { in = out = s+1;
      start = in-1;
    }
  }
#endif

  while( in[0] == '/' && in[1] == '.' && in[2] == '.' && in[3] == '/' )
    in += 3;
  while( in[0] == '.' && in[1] == '/' )
    in += 2;
  if ( in[0] == '/' )
    *out++ = '/';
  initBuffer(&saveb);
  addBuffer(&saveb, out, char*);

  while(*in)
  { if (*in == '/')
    {
    again:
      if ( *in )
      { while( in[1] == '/' )		/* delete multiple / */
	  in++;
	if ( in[1] == '.' )
	{ if ( in[2] == '/' )		/* delete /./ */
	  { in += 2;
	    goto again;
	  }
	  if ( in[2] == EOS )		/* delete trailing /. */
	  { *out = EOS;
	    goto out;
	  }
	  if ( in[2] == '.' && (in[3] == '/' || in[3] == EOS) )
	  { if ( out[-1] == '.' && out[-2] == '.' &&
		 (out-2 == start || out[-3] == '/') )
	    { strncpy(out, in, 3);
	      in += 3;
	      out += 3;
	    } else
	    { if ( !isEmptyBuffer(&saveb) )		/* delete /foo/../ */
	      { out = popBuffer(&saveb, char*);
		in += 3;
		if ( in[0] == EOS && out > start+1 )
		{ out[-1] = EOS;		/* delete trailing / */
		  goto out;
		}
		goto again;
	      } else if (	start[0] == '/' && out == start+1 )
	      { in += 3;
		goto again;
	      }
	    }
	  }
	}
      }
      if ( *in )
	in++;
      if ( out > path && out[-1] != '/' )
	*out++ = '/';
      addBuffer(&saveb, out, char*);
    } else
      *out++ = *in++;
  }
  *out++ = *in++;

out:
  discardBuffer(&saveb);

  return path;
}


static char *
utf8_path_lwr(char *s, size_t len)
{ char buf[PATH_MAX];
  char *tmp = buf;
  char *o=s;
  const char *i;

  if ( len > sizeof(buf) )
  { if ( !(tmp = malloc(len)) )
      return NULL;
  }

  strcpy(tmp, s);
#ifdef O_NETBIOS
  i = skipNetBIOSName(tmp);
  if ( i )
  { memcpy(o, tmp, i-tmp);
    o += i-tmp;
  } else
  { i = tmp;
  }
#else
  i = tmp;
#endif

  while( *i )
  { int c;

    PL_utf8_code_point(&i, NULL, &c);
    c = makeLowerW(c);
    if ( o >= s + PATH_MAX-6 )
    { char ls[10];
      char *e = utf8_put_char(ls,c);
      if ( o+(e-ls) >= s + PATH_MAX )
      { errno = ENAMETOOLONG;
	s = NULL;
	goto out;
      }
    }
    o = utf8_put_char(o, c);
  }
  *o = EOS;

out:
  if ( tmp && tmp != buf )
    free(tmp);

  return s;
}


char *
canonicalisePath(char *path, size_t buflen)
{ GET_LD

  if ( !truePrologFlag(PLFLAG_FILE_CASE) )
  { if ( !utf8_path_lwr(path, PATH_MAX) )
    { if ( errno == ENAMETOOLONG )
	return PL_representation_error("max_path_length"),NULL;
      else
	return PL_resource_error("memory"),NULL;
    }
  }

  if ( !canonicaliseFileName(path, buflen) )
    return NULL;

#ifdef O_CANONICALISE_DIRS
{ char *e;
  char dirname[PATH_MAX];
  size_t plen = strlen(path);

  if ( plen > 0 )
  { e = path + plen - 1;
    for( ; *e != '/' && e > path; e-- )
      ;
    strncpy(dirname, path, e-path);
    dirname[e-path] = EOS;
    canonicaliseDir(dirname);
    strcat(dirname, e);
    strcpy(path, dirname);
  }
}
#endif

  return path;
}


static char *
takeWord(const char **string, char *wrd, int maxlen)
{ const char *s = *string;
  char *q = wrd;
  int left = maxlen-1;

  while( isAlpha(*s) || *s == '_' )
  { if ( --left < 0 )
    { PL_error(NULL, 0, NULL, ERR_REPRESENTATION,
	       ATOM_max_variable_length);
      return NULL;
    }
    *q++ = *s++;
  }
  *q = EOS;

  *string = s;
  return wrd;
}


char *
expandVars(const char *pattern, char *expanded, int maxlen)
{ GET_LD
  int size = 0;
  char wordbuf[PATH_MAX];
  char *rc = expanded;

  if ( *pattern == '~' )
  { char *user;
    char *value;
    int l;

    pattern++;
    user = takeWord(&pattern, wordbuf, sizeof(wordbuf));
    PL_LOCK(L_OS);

    if ( user[0] == EOS )		/* ~/bla */
    {
#ifdef O_XOS
      value = _xos_home();
#else /*O_XOS*/
      if ( !(value = GD->os.myhome) )
      { char envbuf[PATH_MAX];

	if ( (value = Getenv("HOME", envbuf, sizeof(envbuf))) &&
	     (value = PrologPath(value, wordbuf, sizeof(wordbuf))) )
	{ GD->os.myhome = store_string(value);
	} else
	{ value = GD->os.myhome = store_string("/");
	}
      }
#endif /*O_XOS*/
    } else				/* ~fred */
#ifdef HAVE_GETPWNAM
    { struct passwd *pwent;

      if ( GD->os.fred && streq(GD->os.fred, user) )
      { value = GD->os.fredshome;
      } else
      { if ( !(pwent = getpwnam(user)) )
	{ if ( truePrologFlag(PLFLAG_FILEERRORS) )
	  { term_t name = PL_new_term_ref();

	    PL_put_atom_chars(name, user);
	    PL_error(NULL, 0, NULL, ERR_EXISTENCE, ATOM_user, name);
	  }
	  PL_UNLOCK(L_OS);
	  fail;
	}
	if ( GD->os.fred )
	  remove_string(GD->os.fred);
	if ( GD->os.fredshome )
	  remove_string(GD->os.fredshome);

	GD->os.fred = store_string(user);
	value = GD->os.fredshome = store_string(pwent->pw_dir);
      }
    }
#else
    { if ( truePrologFlag(PLFLAG_FILEERRORS) )
	PL_error(NULL, 0, NULL, ERR_NOT_IMPLEMENTED, "user_info");

      PL_UNLOCK(L_OS);
      fail;
    }
#endif
    size += (l = (int) strlen(value));
    if ( size+1 >= maxlen )
    { PL_error(NULL, 0, NULL, ERR_REPRESENTATION, ATOM_max_path_length);
      return NULL;
    }
    strcpy(expanded, value);
    expanded += l;
    PL_UNLOCK(L_OS);

					/* ~/ should not become // */
    if ( expanded[-1] == '/' && pattern[0] == '/' )
      pattern++;
  }

  for( ;; )
  { int c = *pattern++;

    switch( c )
    { case EOS:
	break;
      case '$':
	{ char envbuf[PATH_MAX];
	  char *var = takeWord(&pattern, wordbuf, sizeof(wordbuf));
	  char *value;
	  int l;

	  if ( var[0] == EOS )
	    goto def;
	  PL_LOCK(L_OS);
	  value = Getenv(var, envbuf, sizeof(envbuf));
	  if ( value == (char *) NULL )
	  { if ( truePrologFlag(PLFLAG_FILEERRORS) )
	    { term_t name = PL_new_term_ref();

	      PL_put_atom_chars(name, var);
	      PL_error(NULL, 0, NULL, ERR_EXISTENCE, ATOM_variable, name);
	    }

	    PL_UNLOCK(L_OS);
	    fail;
	  }
	  size += (l = (int)strlen(value));
	  if ( size+1 >= maxlen )
	  { PL_UNLOCK(L_OS);
	    PL_error(NULL, 0, NULL, ERR_REPRESENTATION,
		     ATOM_max_path_length);
	    return NULL;
	  }
	  strcpy(expanded, value);
	  PL_UNLOCK(L_OS);

	  expanded += l;

	  continue;
	}
      default:
      def:
	size++;
	if ( size+1 >= maxlen )
	{ PL_error(NULL, 0, NULL, ERR_REPRESENTATION,
		   ATOM_max_path_length);
	  return NULL;
	}
	*expanded++ = (char)c;

	continue;
    }
    break;
  }

  if ( ++size >= maxlen )
  { PL_error(NULL, 0, NULL, ERR_REPRESENTATION,
	     ATOM_max_path_length);
    return NULL;
  }

  *expanded = EOS;

  return rc;
}


#ifdef O_HASDRIVES

#define IS_DIR_SEPARATOR(c) ((c) == '/' || (c) == '\\')

int
IsAbsolutePath(const char *p)				/* /d:/ */
{ if ( p[0] == '/' && p[2] == ':' && isLetter(p[1]) &&
       (p[3] == '/' || p[3] == '\0') )
    succeed;

#ifdef __MINGW32__ /* /c/ in MINGW is the same as c: */
  if ( p[0] == '/' && isLetter(p[1]) &&
       (p[2] == '/' || p[2] == '\0') )
    succeed;
#endif

  if ( p[1] == ':' && isLetter(p[0]) )		/* d: */
    succeed;

#ifdef O_HASSHARES
  if ( (p[0] == '/' && p[1] == '/') ||	/* //host/share */
       (p[0] == '\\' && p[1] == '\\') )	/* \\host\share */
    succeed;
#endif
  if ( file_name_is_iri(p) )
    succeed;

  fail;
}


static inline int
isDriveRelativePath(const char *p)	/* '/...' */
{ return IS_DIR_SEPARATOR(p[0]) && !IsAbsolutePath(p);
}

#ifdef __WINDOWS__
#undef mkdir
#include <direct.h>
#define mkdir _xos_mkdir
#endif

static int
GetCurrentDriveLetter()
{
#ifdef OS2
  return _getdrive();
#endif
#ifdef __WINDOWS__
  return _getdrive() + 'a' - 1;
#endif
#ifdef __WATCOMC__
  { unsigned drive;
    _dos_getdrive(&drive);
    return = 'a' + drive - 1;
  }
#endif
}

#else /*O_HASDRIVES*/

int
IsAbsolutePath(const char *p)
{ return ( p[0] == '/' ||
	   file_name_is_iri(p) );
}

#endif /*O_HASDRIVES*/

#define isRelativePath(p) ( p[0] == '.' )


char *
AbsoluteFile(const char *spec, char *path, size_t buflen)
{ GET_LD
  char tmp[PATH_MAX];
  char buf[PATH_MAX];
  char *file = PrologPath(spec, buf, sizeof(buf));
  size_t cwdlen;

  if ( !file )
    return (char *) NULL;
  if ( truePrologFlag(PLFLAG_FILEVARS) )
  { if ( !(file = expandVars(buf, tmp, sizeof(tmp))) )
      return (char *) NULL;
  }

  if ( IsAbsolutePath(file) )
  { strcpy(path, file);

    return canonicalisePath(path, buflen);
  }

#ifdef O_HASDRIVES
  if ( isDriveRelativePath(file) )	/* /something  --> d:/something */
  { if ((strlen(file) + 3) > PATH_MAX)
    { PL_error(NULL, 0, NULL, ERR_REPRESENTATION, ATOM_max_path_length);
      return (char *) NULL;
    }
    path[0] = GetCurrentDriveLetter();
    path[1] = ':';
    strcpy(&path[2], file);
    return canonicalisePath(path, buflen);
  }
#endif /*O_HASDRIVES*/

  if ( !PL_cwd(path, PATH_MAX) )
    return NULL;
  cwdlen = strlen(path);

  if ( (cwdlen + strlen(file) + 1) >= PATH_MAX )
  { PL_error(NULL, 0, NULL, ERR_REPRESENTATION, ATOM_max_path_length);
    return (char *) NULL;
  }

  strcpy(&path[cwdlen], file);

  return canonicalisePath(path, buflen);
}


void
PL_changed_cwd(void)
{ PL_LOCK(L_OS);
  if ( GD->paths.CWDdir )
    remove_string(GD->paths.CWDdir);
  GD->paths.CWDdir = NULL;
  GD->paths.CWDlen = 0;
  PL_UNLOCK(L_OS);
}


static char *
cwd_unlocked(char *cwd, size_t cwdlen)
{ GET_LD

  if ( GD->paths.CWDlen == 0 )
  { char buf[PATH_MAX];
    char *rval;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
On SunOs, getcwd() is using popen() to read the output of /bin/pwd.  This
is slow and appears not to cooperate with profile/3.  getwd() is supposed
to be implemented directly.  What about other Unixes?
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(HAVE_GETWD) && (defined(__sun__) || defined(__sun))
#undef HAVE_GETCWD
#endif

#if defined(HAVE_GETWD) && !defined(HAVE_GETCWD)
    rval = getwd(buf);
#else
    rval = getcwd(buf, sizeof(buf));
#endif
    if ( !rval )
    { term_t tmp = PL_new_term_ref();

      PL_put_atom_chars(tmp, ".");
      PL_error(NULL, 0, MSG_ERRNO, ERR_FILE_OPERATION,
	       ATOM_getcwd, ATOM_directory, tmp);

      return NULL;
    }

    if ( !canonicalisePath(buf, sizeof(buf)) )
    { PL_representation_error("max_path_length");
      return NULL;
    }
    GD->paths.CWDlen = strlen(buf);
    buf[GD->paths.CWDlen++] = '/';
    buf[GD->paths.CWDlen] = EOS;

    if ( GD->paths.CWDdir )
      remove_string(GD->paths.CWDdir);
    GD->paths.CWDdir = store_string(buf);
  }

  if ( GD->paths.CWDlen < cwdlen )
  { memcpy(cwd, GD->paths.CWDdir, GD->paths.CWDlen+1);
    return cwd;
  } else
  { PL_representation_error("max_path_length");
    return NULL;
  }
}


char *
PL_cwd(char *cwd, size_t cwdlen)
{ char *rc;

  PL_LOCK(L_OS);
  rc = cwd_unlocked(cwd, cwdlen);
  PL_UNLOCK(L_OS);

  return rc;
}


char *
BaseName(const char *f, char *base)
{ if ( f )
  { char *e = (char*)f+strlen(f);
    char *end;

    if ( e == f )
    { base[0] = EOS;
    } else
    { while(e>f && e[-1] == '/')
	e--;
      end = e;
      while(e>f && e[-1] != '/')
	e--;

      if ( e == end && *e == '/' )
      { strcpy(base, "/");
      } else if ( end-e+1 <= PATH_MAX )
      { memmove(base, e, end-e);		/* may overlap */
	base[end-e] = EOS;
      } else
      { errno = ENAMETOOLONG;
	return NULL;
      }
    }

    return base;
  }

  return NULL;
}


char *
DirName(const char *f, char *dir)
{ if ( f )
  { char *e = (char*)f+strlen(f);

    if ( e == f )
    { strcpy(dir, ".");
    } else
    { while(e>f && e[-1] == '/')
	e--;
      while(e>f && e[-1] != '/')
	e--;
      while(e>f && e[-1] == '/')
	e--;

      if ( e == f )
      { if ( *f == '/' )
	  strcpy(dir, "/");
	else
	  strcpy(dir, ".");
      } else
      { if ( dir != f )			/* otherwise it is in-place */
	{ if ( e-f+1 <= PATH_MAX )
	  { strncpy(dir, f, e-f);
	    dir[e-f] = EOS;
	  } else
	  { errno = ENAMETOOLONG;
	    return NULL;
	  }
	} else
	  e[0] = EOS;
      }
    }

    return dir;
  }

  return NULL;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    bool ChDir(path)
	 char *path;

    Change the current working directory to `path'.  File names may depend
    on `path'.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
is_cwd(const char *dir)
{ int rc;

  PL_LOCK(L_OS);
  rc = (GD->paths.CWDdir && streq(dir, GD->paths.CWDdir));
  PL_UNLOCK(L_OS);

  return rc;
}


bool
ChDir(const char *path)
{ char ospath[PATH_MAX];
  char tmp[PATH_MAX];

  OsPath(path, ospath);

  if ( path[0] == EOS || streq(path, ".") || is_cwd(path) )
    return true;

  if ( !AbsoluteFile(path, tmp, sizeof(tmp)) )
    return false;
  if ( is_cwd(tmp) )
    return true;

  if ( chdir(ospath) == 0 )
  { size_t len;

    len = strlen(tmp);
    if ( len == 0 || tmp[len-1] != '/' )
    { tmp[len++] = '/';
      tmp[len] = EOS;
    }
    PL_LOCK(L_OS);				/* Lock with PL_changed_cwd() */
    GD->paths.CWDlen = len;			/* and PL_cwd() */
    if ( GD->paths.CWDdir )
      remove_string(GD->paths.CWDdir);
    GD->paths.CWDdir = store_string(tmp);
    PL_UNLOCK(L_OS);

    return true;
  }

  return false;
}


		/********************************
		*        TIME CONVERSION        *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    struct tm *PL_localtime_r(time_t time, struct tm *r)

    Convert time in Unix internal form (seconds since Jan 1 1970) into a
    structure providing easier access to the time.

    For non-Unix systems: struct time is supposed  to  look  like  this.
    Move  This  definition to pl-os.h and write the conversion functions
    here.

    struct tm {
	int	tm_sec;		/ * second in the minute (0-59)* /
	int	tm_min;		/ * minute in the hour (0-59) * /
	int	tm_hour;	/ * hour of the day (0-23) * /
	int	tm_mday;	/ * day of the month (1-31) * /
	int	tm_mon;		/ * month of the year (1-12) * /
	int	tm_year;	/ * year (0 = 1900) * /
	int	tm_wday;	/ * day in the week (1-7, 1 = sunday) * /
	int	tm_yday;	/ * day in the year (0-365) * /
	int	tm_isdst;	/ * daylight saving time info * /
    };

    time_t Time()

    Return time in seconds after Jan 1 1970 (Unix' time notion).

Note: MinGW has localtime_r(),  but  it  is   not  locked  and  thus not
thread-safe. MinGW does not have localtime_s(), but   we  test for it in
configure.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

struct tm *
PL_localtime_r(const time_t *t, struct tm *r)
{
#ifdef HAVE_LOCALTIME_R
  return localtime_r(t, r);
#else
#ifdef HAVE_LOCALTIME_S
  return localtime_s(r, t) == EINVAL ? NULL : t;
#else
  struct tm *rc;

  PL_LOCK(L_OS);
  if ( (rc = localtime(t)) )
    *r = *rc;
  else
    r = NULL;
  PL_UNLOCK(L_OS);

  return r;
#endif
#endif
}

char *
PL_asctime_r(const struct tm *tm, char *buf)
{
#ifdef HAVE_ASCTIME_R
  return asctime_r(tm, buf);
#else
  char *rc;

  PL_LOCK(L_OS);
  if ( (rc = asctime(tm)) )
    strcpy(buf, rc);
  else
    buf = NULL;
  PL_UNLOCK(L_OS);

  return buf;
#endif
}


		 /*******************************
		 *	      TERMINAL		*
		 *******************************/

#ifdef HAVE_TCSETATTR
#include <termios.h>
#include <unistd.h>
#define O_HAVE_TERMIO 1
#else /*HAVE_TCSETATTR*/
#ifdef HAVE_SYS_TERMIO_H
#include <sys/termio.h>
#define termios termio
#define O_HAVE_TERMIO 1
#else
#ifdef HAVE_SYS_TERMIOS_H
#include <sys/termios.h>
#define O_HAVE_TERMIO 1
#endif
#endif
#endif /*HAVE_TCSETATTR*/

typedef struct tty_state
{
#if defined(O_HAVE_TERMIO)
  struct termios tab;
#elif defined(HAVE_SGTTYB)
  struct sgttyb tab;
#else
  int tab;				/* empty is not allowed */
#endif
} tty_state;

#define TTY_STATE(buf) (((tty_state*)((buf)->state))->tab)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
			TERMINAL IO MANIPULATION

ResetStdin()
    Clear the Sinput buffer after a saved state.  Only necessary
    if O_SAVE is defined.

PushTty(IOSTREAM *s, ttybuf *buf, int state)
    Push the tty to the specified state and save the old state in
    buf.

PopTty(IOSTREAM *s, ttybuf *buf)
    Restore the tty state.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
Sttymode(IOSTREAM *s)
{ return ison(s, SIO_RAW) ? TTY_RAW : TTY_COOKED;
}

static void
Sset_ttymode(IOSTREAM *s, int mode)
{ if ( mode == TTY_RAW )
    set(s, SIO_RAW);
  else
    clear(s, SIO_RAW);
}


static void
ResetStdin(void)
{ Sinput->limitp = Sinput->bufp = Sinput->buffer;
  if ( !GD->os.org_terminal.read )
    GD->os.org_terminal = *Sinput->functions;
}

static ssize_t
Sread_terminal(void *handle, char *buf, size_t size)
{ GET_LD
  ssize_t rc;
  source_location oldsrc = LD->read_source;

  if ( Sinput->handle == handle )
  { if ( LD->prompt.next &&
	 isoff(Sinput, SIO_RAW) &&
	 ison(Sinput, SIO_ISATTY) )
      PL_write_prompt(true);
    else if ( ison(Soutput, SIO_ISATTY) )
      Sflush(Suser_output);

    PL_dispatch(Sinput, PL_DISPATCH_WAIT);
    rc = (*GD->os.org_terminal.read)(handle, buf, size);

    if ( rc == 0 )			/* end-of-file */
    { if ( Sinput == Suser_input )
      { Sclearerr(Suser_input);
	LD->prompt.next = true;
      }
    } else if ( rc > 0 && buf[rc-1] == '\n' )
      LD->prompt.next = true;

    LD->read_source = oldsrc;
  } else
  { rc = (*GD->os.org_terminal.read)(handle, buf, size);
  }

  return rc;
}

void
ResetTty(void)
{ GET_LD

  ResetStdin();
  if ( !GD->os.iofunctions.read )
  { GD->os.iofunctions       = *Sinput->functions;
    GD->os.iofunctions.read  = Sread_terminal;

    Sinput->functions  =
    Soutput->functions =
    Serror->functions  = &GD->os.iofunctions;
  }
  LD->prompt.next = true;
}

#ifdef O_HAVE_TERMIO			/* sys/termios.h or sys/termio.h */

#ifndef HAVE_TCSETATTR
#ifndef NO_SYS_IOCTL_H_WITH_SYS_TERMIOS_H
#include <sys/ioctl.h>
#endif
#ifndef TIOCGETA
#define TIOCGETA TCGETA
#endif
#endif

static int
GetTtyState(int fd, struct termios *tio)
{ memset(tio, 0, sizeof(*tio));

#ifdef HAVE_TCSETATTR
  if ( tcgetattr(fd, tio) )
    return false;
#else
  if ( ioctl(fd, TIOCGETA, tio) )
    return false;
#endif

  return true;
}

static int
SetTtyState(int fd, struct termios *tio)
{
#ifdef HAVE_TCSETATTR
  if ( tcsetattr(fd, TCSANOW, tio) != 0 )
  { static int MTOK_warned;			/* MT-OK */

    if ( !MTOK_warned++ )
      return warning("Failed to set terminal: %s", OsError());
  }
#else
#ifdef TIOCSETAW
  ioctl(fd, TIOCSETAW, tio);
#else
  ioctl(fd, TCSETAW, tio);
  ioctl(fd, TCXONC, (void *)1);
#endif
#endif

  if ( fd == ttyfileno && ttytab.state )
    ttymodified = memcmp(&TTY_STATE(&ttytab), tio, sizeof(*tio));

  return true;
}


bool
PushTty(IOSTREAM *s, ttybuf *buf, int mode)
{ GET_LD
  struct termios tio;
  int fd;

  buf->mode  = Sttymode(s);
  buf->state = NULL;

  if ( isoff(s, SIO_ISATTY) )
  { DEBUG(MSG_TTY, Sdprintf("stdin is not a terminal\n"));
    succeed;				/* not a terminal */
  }
  if ( !truePrologFlag(PLFLAG_TTY_CONTROL) )
  { DEBUG(MSG_TTY, Sdprintf("tty_control is false\n"));
    succeed;
  }

  Sset_ttymode(s, mode);

  if ( (fd = Sfileno(s)) < 0 || !isatty(fd) )
    succeed;

  buf->state = allocHeapOrHalt(sizeof(tty_state));

  if ( !GetTtyState(fd, &TTY_STATE(buf)) )
    return false;

  tio = TTY_STATE(buf);			/* structure copy */

  switch( mode )
  { case TTY_RAW:
#if defined(HAVE_TCSETATTR) && defined(HAVE_CFMAKERAW)
	cfmakeraw(&tio);
	tio.c_oflag = TTY_STATE(buf).c_oflag;	/* donot change output modes */
	tio.c_lflag |= ISIG;
#else
	tio.c_lflag &= ~(ECHO|ICANON);
#endif
					/* OpenBSD requires this anyhow!? */
					/* Bug in OpenBSD or must we? */
					/* Could this do any harm? */
	tio.c_cc[VTIME] = 0, tio.c_cc[VMIN] = 1;
	break;
    case TTY_SAVE:
	succeed;
    default:
	sysError("Unknown PushTty() mode: %d", mode);
	/*NOTREACHED*/
  }

  return SetTtyState(fd, &tio);
}

/**
 * @param do_free is one of
 *   - false: do not free the state
 *   - true:  free the state
 */

bool
PopTty(IOSTREAM *s, ttybuf *buf, int do_free)
{ int rc = true;

  Sset_ttymode(s, buf->mode);

  if ( buf->state )
  { GET_LD
    int fd;

    if ( (!HAS_LD || truePrologFlag(PLFLAG_TTY_CONTROL)) &&
	 (fd = Sfileno(s)) >= 0 )
    { DEBUG(MSG_TTY,
	    Sdprintf("HAS_LD = %d; tty_control = %d\n",
		     HAS_LD, truePrologFlag(PLFLAG_TTY_CONTROL)));
      rc = SetTtyState(fd, &TTY_STATE(buf));
    }

    if ( do_free )
    { freeHeap(buf->state, sizeof(tty_state));
      buf->state = NULL;
    }
  }

  return rc;
}

#else /* O_HAVE_TERMIO */

#ifdef HAVE_SGTTYB

bool
PushTty(IOSTREAM *s, ttybuf *buf, int mode)
{ struct sgttyb tio;
  int fd;

  buf->mode = Sttymode(s);
  buf->state = NULL;

  if ( (fd = Sfileno(s)) < 0 || !isatty(fd) )
    succeed;				/* not a terminal */
  if ( !truePrologFlag(PLFLAG_TTY_CONTROL) )
    succeed;

  Sset_ttymode(s, mode);
  buf->state = allocHeapOrHalt(sizeof((*buf->state));
  memset(buf->state, 0, sizeof(*buf->state));

  if ( ioctl(fd, TIOCGETP, &TTY_STATE(buf)) )  /* save the old one */
    fail;
  tio = TTY_STATE(buf);

  switch( mode )
  { case TTY_RAW:
      tio.sg_flags |= CBREAK;
      tio.sg_flags &= ~ECHO;
      break;
    case TTY_SAVE:
      succeed;
    default:
      sysError("Unknown PushTty() mode: %d", mode);
      /*NOTREACHED*/
  }

  ioctl(fd, TIOCSETP,  &tio);
  ioctl(fd, TIOCSTART, NULL);

  succeed;
}


bool
PopTty(IOSTREAM *s, ttybuf *buf, int do_free)
{ Sset_ttymode(s, buf->mode);
  if ( buf->state )
  { int fd = Sfileno(s);

    if ( fd >= 0 )
    { ioctl(fd, TIOCSETP,  &buf->tab);
      ioctl(fd, TIOCSTART, NULL);
    }

    if ( do_free )
    { freeHeap(buf->state, sizeof(tty_state));
      buf->state = NULL;
    }
  }

  succeed;
}

#else /*HAVE_SGTTYB*/

bool
PushTty(IOSTREAM *s, ttybuf *buf, int mode)
{ buf->mode = Sttymode(s);
  Sset_ttymode(s, mode);

  succeed;
}


bool
PopTty(IOSTREAM *s, ttybuf *buf, int do_free)
{ GET_LD

  Sset_ttymode(s, buf->mode);
  if ( buf->mode != TTY_RAW )
    LD->prompt.next = true;

  succeed;
}

#endif /*HAVE_SGTTYB*/
#endif /*O_HAVE_TERMIO*/


		/********************************
		*      ENVIRONMENT CONTROL      *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Simple  library  to  manipulate  the    OS   environment.  The  modified
environment will be passed to  child  processes   and  the  can  also be
requested via getenv/2 from Prolog. Functions

    int Setenv(name, value)
	 char *name, *value;

    Set the OS environment variable with name `name'.   If  it  exists
    its  value  is  changed, otherwise a new entry in the environment is
    created.  The return value is a pointer to the old value, or NULL if
    the variable is new.

    int Unsetenv(name)
	 char *name;

    Delete a variable from the environment.  Return  value  is  the  old
    value, or NULL if the variable did not exist.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

size_t
getenv3(const char *name, char *buf, size_t len)
{
#if O_XOS
  return _xos_getenv(name, buf, len);
#else
  char *s = getenv(name);
  size_t l;

  if ( s )
  { if ( (l=strlen(s)) < len )
      memcpy(buf, s, l+1);
    else if ( len > 0 )
      buf[0] = EOS;                     /* empty string if not fit */

    return l;
  }

  return (size_t)-1;
#endif
}


char *
Getenv(const char *name, char *buf, size_t len)
{ size_t l = getenv3(name, buf, len);

  if ( l != (size_t)-1 && l < len )
    return buf;

  return NULL;
}


#if defined(HAVE_PUTENV) || defined(HAVE_SETENV)

int
Setenv(char *name, char *value)
{
#ifdef HAVE_SETENV
  if ( setenv(name, value, true) != 0 )
    return PL_error(NULL, 0, MSG_ERRNO, ERR_SYSCALL, "setenv");
#else
  char *buf;

  if ( *name == '\0' || strchr(name, '=') != NULL )
  { errno = EINVAL;
    return PL_error(NULL, 0, MSG_ERRNO, ERR_SYSCALL, "setenv");
  }

  buf = alloca(strlen(name) + strlen(value) + 2);

  if ( buf )
  { Ssprintf(buf, "%s=%s", name, value);

    if ( putenv(store_string(buf)) < 0 )
      return PL_error(NULL, 0, MSG_ERRNO, ERR_SYSCALL, "setenv");
  } else
    return PL_error(NULL, 0, NULL, ERR_NOMEM);
#endif
  succeed;
}

int
Unsetenv(char *name)
{
#ifdef HAVE_UNSETENV
#ifdef VOID_UNSETENV
  unsetenv(name);
#else
  if ( unsetenv(name) < 0 )
    return PL_error(NULL, 0, MSG_ERRNO, ERR_SYSCALL, "unsetenv");
#endif

  succeed;
#else
  if ( !getenv(name) )
    succeed;

  return Setenv(name, "");
#endif
}

static void
initEnviron()
{
}

#else /*HAVE_PUTENV*/

extern char **environ;		/* Unix predefined environment */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Grow the environment array by one and return the (possibly  moved)  base
pointer to the new environment.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

forwards char	**growEnviron(char**, int);
forwards char	*matchName(const char *, const char *);
forwards void	setEntry(char **, char *, char *);

static char **
growEnviron(char **e, int amount)
{ static int filled;
  static int size = -1;

  if ( amount == 0 )			/* reset after a dump */
  { size = -1;
    return e;
  }

  if ( size < 0 )
  { char **env, **e1, **e2;

    for(e1=e, filled=0; *e1; e1++, filled++)
      ;
    size = ROUND(filled+10+amount, 32);
    env = (char **)PL_malloc(size * sizeof(char *));
    for ( e1=e, e2=env; *e1; *e2++ = *e1++ )
      ;
    *e2 = (char *) NULL;
    filled += amount;

    return env;
  }

  filled += amount;
  if ( filled + 1 > size )
  { char **env, **e1, **e2;

    size += 32;
    env = (char **)PL_realloc(e, size * sizeof(char *));
    for ( e1=e, e2=env; *e1; *e2++ = *e1++ )
      ;
    *e2 = (char *) NULL;

    return env;
  }

  return e;
}


static void
initEnviron(void)
{ growEnviron(environ, 0);
}


static char *
matchName(const char *e, const char *name)
{ while( *name && *e == *name )
    e++, name++;

  if ( (*e == '=' || *e == EOS) && *name == EOS )
    return (char *)(*e == '=' ? e+1 : e);

  return (char *)NULL;
}


static void
setEntry(char **e, char *name, char *value)
{ size_t l = strlen(name);

  *e = PL_malloc_atomic(l + strlen(value) + 2);
  strcpy(*e, name);
  e[0][l++] = '=';
  strcpy(&e[0][l], value);
}


char *
Setenv(char *name, char *value)
{ char **e;
  char *v;
  int n;

  for(n=0, e=environ; *e; e++, n++)
  { if ( (v=matchName(*e, name)) != NULL )
    { if ( !streq(v, value) )
	setEntry(e, name, value);
      return v;
    }
  }
  environ = growEnviron(environ, 1);
  setEntry(&environ[n], name, value);
  environ[n+1] = (char *) NULL;

  return (char *) NULL;
}


char *
Unsetenv(char *name)
{ char **e;
  char *v;
  int n;

  for(n=0, e=environ; *e; e++, n++)
  { if ( (v=matchName(*e, name)) != NULL )
    { environ = growEnviron(environ, -1);
      e = &environ[n];
      do
      { e[0] = e[1];
	e++;
      } while(*e);

      return v;
    }
  }

  return (char *) NULL;
}

#endif /*HAVE_PUTENV*/

		/********************************
		*       SYSTEM PROCESSES        *
		*********************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    int System(command)
	char *command;

    Invoke a command on the operating system.  The return value  is  the
    exit  status  of  the  command.   Return  value  0 implies succesful
    completion. If you are not running Unix your C-library might provide
    an alternative.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef __unix__
#define SPECIFIC_SYSTEM 1

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
According to the autoconf docs HAVE_SYS_WAIT_H   is set if sys/wait.h is
defined *and* is POSIX.1 compliant,  which   implies  it uses int status
argument to wait()
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef HAVE_SYS_WAIT_H
#undef UNION_WAIT
#include <sys/wait.h>
#define wait_t int

#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#else /*HAVE_SYS_WAIT_H*/

#ifdef UNION_WAIT			/* Old BSD style wait */
#include <sys/wait.h>
#define wait_t union wait

#ifndef WEXITSTATUS
#define WEXITSTATUS(s) ((s).w_status)
#endif
#ifndef WTERMSIG
#define WTERMSIG(s) ((s).w_status)
#endif
#endif /*UNION_WAIT*/

#endif /*HAVE_SYS_WAIT_H*/

static const char *
prog_shell(void)
{ GET_LD

#ifdef O_PLMT
  if ( LD )
#endif
  { atom_t a;

    if ( PL_current_prolog_flag(ATOM_posix_shell, PL_ATOM, &a) )
    { term_t t;
      char *s;

      if ( (t=PL_new_term_ref()) &&
	   PL_put_atom(t, a) &&
	   PL_get_chars(t, &s, CVT_ATOM|REP_MB) )
	return s;
    }
  }

  return POSIX_SHELL;
}


int
System(char *cmd)
{ GET_LD
  int pid;
  const char *shell = prog_shell();
  int rval;
#if O_SIGNALS
  void (*old_int)();
  void (*old_stop)();
#endif

  if ( (pid = fork()) == -1 )
  { return PL_error("shell", 2, MSG_ERRNO, ERR_SYSCALL, "fork");
  } else if ( pid == 0 )		/* The child */
  { char tmp[PATH_MAX];
    char *argv[4];
    extern char **environ;
    int in  = Sfileno(Suser_input);
    int out = Sfileno(Suser_output);
    int err = Sfileno(Suser_error);

    if ( in >=0 && out >= 0 && err >= 0 )
    { if ( dup2(in,  0) < 0 ||
	   dup2(out, 1) < 0 ||
	   dup2(err, 2) < 0 )
	Sdprintf("shell/1: dup of file descriptors failed\n");
    }

    argv[0] = BaseName(shell, tmp);
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = NULL;

    Setenv("PROLOGCHILD", "yes");
    PL_cleanup_fork();
    execve(shell, argv, environ);
    fatalError("Failed to execute %s: %s", shell, OsError());
    fail;
    /*NOTREACHED*/
  } else
  { wait_t status;			/* the parent */
    int n;

#if O_SIGNALS
    old_int  = signal(SIGINT,  SIG_IGN);
#ifdef SIGTSTP
    old_stop = signal(SIGTSTP, SIG_DFL);
#endif /* SIGTSTP */
#endif

    for(;;)
    {
#ifdef HAVE_WAITPID
      n = waitpid(pid, &status, 0);
#else
      n = wait(&status);
#endif
      if ( n == -1 && errno == EINTR )
	continue;
      if ( n != pid )
	continue;
      break;
    }

    if ( n == -1 )
    { term_t tmp = PL_new_term_ref();

      PL_put_atom_chars(tmp, cmd);
      PL_error("shell", 2, MSG_ERRNO, ERR_SHELL_FAILED, tmp);

      rval = 1;
    } else if (WIFEXITED(status))
    { rval = WEXITSTATUS(status);
#ifdef WIFSIGNALED
    } else if (WIFSIGNALED(status))
    { term_t tmp = PL_new_term_ref();
      int sig = WTERMSIG(status);

      PL_put_atom_chars(tmp, cmd);
      PL_error("shell", 2, NULL, ERR_SHELL_SIGNALLED, tmp, sig);
      rval = 1;
#endif
    } else
    { rval = 1;				/* make gcc happy */
      fatalError("Unknown return code from wait(3)");
      /*NOTREACHED*/
    }
  }

#if O_SIGNALS
  signal(SIGINT,  old_int);		/* restore signal handlers */
#ifdef SIGTSTP
  signal(SIGTSTP, old_stop);
#endif /* SIGTSTP */
#endif

  return rval;
}
#endif /* __unix__ */


#ifdef HAVE_WINEXEC			/* Windows 3.1 */
#define SPECIFIC_SYSTEM 1

int
System(char *command)
{ char *msg;
  int rval = WinExec(command, SW_SHOWNORMAL);

  if ( rval < 32 )
  { switch( rval )
    { case 0:	msg = "Not enough memory"; break;
      case 2:	msg = "File not found"; break;
      case 3:	msg = "No path"; break;
      case 5:	msg = "Unknown error"; break;
      case 6:	msg = "Lib requires separate data segment"; break;
      case 8:	msg = "Not enough memory"; break;
      case 10:	msg = "Incompatible Windows version"; break;
      case 11:	msg = "Bad executable file"; break;
      case 12:	msg = "Incompatible operating system"; break;
      case 13:	msg = "MS-DOS 4.0 executable"; break;
      case 14:	msg = "Unknown executable file type"; break;
      case 15:	msg = "Real-mode application"; break;
      case 16:	msg = "Cannot start multiple copies"; break;
      case 19:	msg = "Executable is compressed"; break;
      case 20:	msg = "Invalid DLL"; break;
      case 21:	msg = "Application is 32-bits"; break;
      default:	msg = "Unknown error";
    }

    warning("Could not start %s: error %d (%s)",
	    command, rval, msg);
    return 1;
  }

  return 0;
}
#endif


#ifdef __WINDOWS__
#define SPECIFIC_SYSTEM 1

					/* definition in pl-nt.c */
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Nothing special is needed.  Just hope the C-library defines system().
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef SPECIFIC_SYSTEM

int
System(command)
char *command;
{ return system(command);
}

#endif


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    char *findExecutable(const char *progname, char *buf, size_t buflen)

    Return the path name of the executable of SWI-Prolog.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef __WINDOWS__			/* Win32 version in pl-nt.c */
static char *	Which(const char *program, char *fullname);

char *
findExecutable(const char *av0, char *buffer, size_t buflen)
{ char *file;
  char buf[PATH_MAX];
  char tmp[PATH_MAX];

  if ( !av0 || !PrologPath(av0, buf, sizeof(buf)) )
    return NULL;
  file = Which(buf, tmp);

#if __unix__				/* argv[0] can be an #! script! */
  if ( file )
  { int n, fd;
    char buf[PATH_MAX];
					/* Fails if mode is x-only, but */
					/* then it can't be a script! */
    if ( (fd = open(file, O_RDONLY)) < 0 )
      return strcpy(buffer, file);
    n = read(fd, buf, sizeof(buf)-1);
    close(fd);

    if ( n > 0 )
    { buf[n] = EOS;
      if ( strncmp(buf, "#!", 2) == 0 )
      { char *s = &buf[2], *q;
	while(*s && isBlank(*s))
	  s++;
	for(q=s; *q && !isBlank(*q); q++)
	  ;
	*q = EOS;

	return strcpy(buffer, s);
      }
    }
  }
#endif /*__unix__*/

  return strcpy(buffer, file ? file : buf);
}


#if defined(OS2) || defined(__DOS__) || defined(__WINDOWS__)
#define EXEC_EXTENSIONS { ".exe", ".com", ".bat", ".cmd", NULL }
#define PATHSEP ';'
#elif defined(__EMSCRIPTEN__)
#define EXEC_EXTENSIONS { ".js", NULL }
#define PATHSEP ':'
#endif

#ifdef EXEC_EXTENSIONS

static char *
okToExec(const char *s)
{ static char *extensions[] = EXEC_EXTENSIONS;
  static char **ext;

  DEBUG(2, Sdprintf("Checking %s\n", s));
  for(ext = extensions; *ext; ext++)
    if ( stripostfix(s, *ext) )
      return ExistsFile(s) ? (char *)s : (char *) NULL;

  for(ext = extensions; *ext; ext++)
  { static char path[PATH_MAX];

    strcpy(path, s);
    strcat(path, *ext);
    if ( ExistsFile(path) )
      return path;
  }

  return (char *) NULL;
}

#else /*EXEC_EXTENSIONS*/

static char *
okToExec(const char *s)
{ statstruct stbuff;

  if (statfunc(s, &stbuff) == 0 &&	/* stat it */
     S_ISREG(stbuff.st_mode) &&		/* check for file */
     access(s, X_OK) == 0)		/* can be executed? */
    return (char *)s;
  else
    return (char *) NULL;
}
#define PATHSEP	':'

#endif /*EXEC_EXTENSIONS*/

static char *
Which(const char *program, char *fullname)
{ char *path, *dir;
  char *e;

  if ( IsAbsolutePath(program) ||
#if OS2 && EMX
       isDriveRelativePath(program) ||
#endif /* OS2 */
       isRelativePath(program) ||
       strchr(program, '/') )
  { if ( (e = okToExec(program)) != NULL )
    { strcpy(fullname, e);

      return fullname;
    }

    return NULL;
  }

#if OS2 && EMX
  if ((e = okToExec(program)) != NULL)
  {
    getcwd(fullname, PATH_MAX);
    strcat(fullname, "/");
    strcat(fullname, e);
    return fullname;
  }
#endif /* OS2 */
  if  ((path = getenv("PATH") ) == 0)
    path = DEFAULT_PATH;

  while(*path)
  { if ( *path == PATHSEP )
    { if ( (e = okToExec(program)) )
	return strcpy(fullname, e);
      else
	path++;				/* fix by Ron Hess (hess@sco.com) */
    } else
    { char tmp[PATH_MAX];

      for(dir = fullname; *path && *path != PATHSEP; *dir++ = *path++)
	;
      if (*path)
	path++;				/* skip : */
      if ((dir-fullname) + strlen(program)+2 > PATH_MAX)
	continue;
      *dir++ = '/';
      strcpy(dir, program);
      if ( (e = okToExec(OsPath(fullname, tmp))) )
	return strcpy(fullname, e);
    }
  }

  return NULL;
}

#endif /*__WINDOWS__*/

/** int Pause(double time)

Suspend execution `time' seconds. Time  is   given  as  a floating point
number, expressing the time  to  sleep   in  seconds.  Just  about every
platform requires it own implementation. We provide them in the order of
preference. The implementations differ on  their granularity and whether
or not they can  be  interrupted   savely  restarted.  The  recent POSIX
nanosleep() is just about the  only   function  that  really works well:
accurate, interruptable and restartable.
*/

#ifdef __WINDOWS__
#define PAUSE_DONE 1			/* see pl-nt.c */
#endif

#if !defined(PAUSE_DONE) && defined(HAVE_NANOSLEEP)
#define PAUSE_DONE 1

int
Pause(double t)
{ struct timespec req;
  int rc;

  if ( t < 0.0 )
    succeed;

  req.tv_sec = (time_t) t;
  req.tv_nsec = (long)((t - floor(t)) * 1000000000);

  for(;;)
  { rc = nanosleep(&req, &req);
    if ( rc == -1 && errno == EINTR )
    { if ( PL_handle_signals() < 0 )
	return false;
    } else
      return true;
  }
}

#endif /*HAVE_NANOSLEEP*/


#if !defined(PAUSE_DONE) && defined(HAVE_USLEEP)
#define PAUSE_DONE 1

int
Pause(double t)
{ if ( t <= 0.0 )
    return true;

  usleep((unsigned long)(t * 1000000.0));

  return true;
}

#endif /*HAVE_USLEEP*/


#if !defined(PAUSE_DONE) && defined(HAVE_SELECT)
#define PAUSE_DONE 1

int
Pause(double time)
{ struct timeval timeout;

  if ( time <= 0.0 )
    return;

  if ( time < 60.0 )		/* select() is expensive. Does it make sense */
  { timeout.tv_sec = (long)time;
    timeout.tv_usec = (long)(time * 1000000) % 1000000;
    select(1, NULL, NULL, NULL, &timeout);

    return true;
  } else
  { int rc;
    int left = (int)(time+0.5);

    do
    { rc = sleep(left);
      if ( rc == -1 && errno == EINTR )
      { if ( PL_handle_signals() < 0 )
	  return false;

	return true;
      }
      left -= rc;
    } while ( rc != 0 );
  }
}

#endif /*HAVE_SELECT*/

#if !defined(PAUSE_DONE) && defined(HAVE_DOSSLEEP)
#define PAUSE_DONE 1

int					/* a millisecond granualrity. */
Pause(double time)			/* the EMX function sleep uses seconds */
{ if ( time <= 0.0 )			/* the select() trick does not work at all. */
    return true;

  DosSleep((ULONG)(time * 1000));

  return true;
}

#endif /*HAVE_DOSSLEEP*/

#if !defined(PAUSE_DONE) && defined(HAVE_SLEEP)
#define PAUSE_DONE 1

int
Pause(double t)
{ if ( t <= 0.5 )
    succeed;

  sleep((int)(t + 0.5));

  succeed;
}

#endif /*HAVE_SLEEP*/

#if !defined(PAUSE_DONE) && defined(HAVE_DELAY)
#define PAUSE_DONE 1

int
Pause(double t)
{ delay((int)(t * 1000));

  return true;
}

#endif /*HAVE_DELAY*/

#ifndef PAUSE_DONE
int
Pause(double t)
{ return notImplemented("sleep", 1);
}
#endif
