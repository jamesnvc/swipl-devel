/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  1995-2024, University of Amsterdam
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

#ifdef __WINDOWS__
#define SWIPL_WINDOWS_NATIVE_ACCESS 1
#include <winsock2.h>			/* Needed on VC8 */
#include <windows.h>
#include <psapi.h>

#ifdef __MINGW32__
#ifndef _WIN32_IE
#define _WIN32_IE 0x0400
#endif
/* FIXME: these are copied from SWI-Prolog.h. */
#define PL_MSG_EXCEPTION_RAISED -1
#define PL_MSG_IGNORED 0
#define PL_MSG_HANDLED 1
#endif

#include "pl-nt.h"
#include "pl-fli.h"
#include "os/pl-utf8.h"
#include <process.h>
#include "os/pl-ctype.h"
#include <stdio.h>
#include <stdarg.h>
#include "os/SWI-Stream.h"
#include <process.h>
#include <winbase.h>
#ifdef HAVE_CRTDBG_H
#include <crtdbg.h>
#endif


		 /*******************************
		 *	       CONSOLE		*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
There is no way to tell which subsystem   an app belongs too, except for
peeking in its executable-header. This is a bit too much ...
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
hasConsole(void)
{ HANDLE h;

  if ( GD->os.gui_app == false )	/* has been set explicitly */
    succeed;

					/* I found a console */

  if ( (h = GetStdHandle(STD_OUTPUT_HANDLE)) != INVALID_HANDLE_VALUE )
  { DWORD mode;

    if ( GetConsoleMode(h, &mode) )
      succeed;
  }

					/* assume we are GUI */
  fail;
}


bool
win_input_ready(IOSTREAM *input)
{ HANDLE hConsole = Swinhandle(input);
  if ( !hConsole )
    return false;
  if ( WaitForSingleObject(hConsole, 0) == WAIT_OBJECT_0 )
  { DWORD count;
    GetNumberOfConsoleInputEvents(hConsole, &count);
    return count > 0;
  }
  return false;
}


bool
PL_wait_for_console_input(IOSTREAM *input)
{ HANDLE hConsole = Swinhandle(input);

  if ( !hConsole )
    return true;

  for(;;)
  { DWORD rc = MsgWaitForMultipleObjects(1,
					 &hConsole,
					 false,	/* wait for either event */
					 INFINITE,
					 QS_ALLINPUT);

    if ( rc == WAIT_OBJECT_0+1 )
    { MSG msg;

      while( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
      { TranslateMessage(&msg);
	DispatchMessage(&msg);
      }
    } else if ( rc == WAIT_OBJECT_0 )
    { return true;
    } else
    { DEBUG(MSG_WIN_API,
	    Sdprintf("MsgWaitForMultipleObjects(): 0x%x\n", rc));
    }
  }
}


		 /*******************************
		 *	    MESSAGE BOX		*
		 *******************************/

void
PlMessage(const char *fm, ...)
{ va_list(args);

  va_start(args, fm);

  if ( hasConsole() )
  { Sfprintf(Serror, "SWI-Prolog: ");
    Svfprintf(Serror, fm, args);
    Sfprintf(Serror, "\n");
  } else
  { char buf[1024];
    int64_t hwndi;
    HWND hwnd = NULL;
    static atom_t ATOM_hwnd = 0;

    if ( !ATOM_hwnd )
      ATOM_hwnd = PL_new_atom("hwnd");

    if ( PL_current_prolog_flag(ATOM_hwnd, PL_INTEGER, &hwndi) )
      hwnd = (HWND)(uintptr_t)hwndi;

    vsprintf(buf, fm, args);
    MessageBox(hwnd, buf, "SWI-Prolog", MB_OK|MB_TASKMODAL);
  }

  va_end(args);
}



		 /*******************************
		 *	WinAPI ERROR CODES	*
		 *******************************/

static const char *
WinErrorNo(int id)
{ char *msg;
  static WORD lang;
  static int lang_initialised = 0;

  if ( !lang_initialised )
    lang = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK);

again:
  if ( FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|
		     FORMAT_MESSAGE_IGNORE_INSERTS|
		     FORMAT_MESSAGE_FROM_SYSTEM,
		     NULL,			/* source */
		     id,			/* identifier */
		     lang,
		     (LPTSTR) &msg,
		     0,				/* size */
		     NULL) )			/* arguments */
  { atom_t a = PL_new_atom(msg);

    LocalFree(msg);
    lang_initialised = 1;

    return stringAtom(a);
  } else
  { if ( lang_initialised == 0 )
    { lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
      lang_initialised = 1;
      goto again;
    }

    return "Unknown Windows error";
  }
}

const char *
WinError(void)
{ int id = GetLastError();

  return WinErrorNo(id);
}

		 /*******************************
		 *	  SLEEP/1 SUPPORT	*
		 *******************************/

int
Pause(double t)
{ HANDLE h;

  if ( t <= 0.0 )
  { SwitchToThread();
    return true;
  }

  if ( (h = CreateWaitableTimer(NULL, true, NULL)) )
  { LARGE_INTEGER ft;

    ft.QuadPart = -(LONGLONG)(t * 10000000.0); /* 100 nanosecs per tick */

    SetWaitableTimer(h, &ft, 0, NULL, NULL, false);
    for(;;)
    { int rc = MsgWaitForMultipleObjects(1,
					 &h,
					 false,
					 INFINITE,
					 QS_ALLINPUT);
      if ( rc == WAIT_OBJECT_0+1 )
      { MSG msg;

	while( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
	{ TranslateMessage(&msg);
	  DispatchMessage(&msg);
	  if ( PL_exception(0) )
	    return false;
	}

	if ( PL_handle_signals() < 0 )
	{ CloseHandle(h);
	  return false;
	}
      } else
	break;
    }
    CloseHandle(h);

    return true;
  } else				/* Pre NT implementation */
  { DWORD msecs = (DWORD)(t * 1000.0);

    while( msecs >= 100 )
    { Sleep(100);
      if ( PL_handle_signals() < 0 )
	return false;
      msecs -= 100;
    }
    if ( msecs > 0 )
      Sleep(msecs);

    return true;
  }
}


		 /*******************************
		 *	  SET FILE SIZE		*
		 *******************************/

#ifndef HAVE_FTRUNCATE

int
ftruncate(int fileno, int64_t length)
{ errno_t e;

  if ( (e=_chsize_s(fileno, length)) == 0 )
    return 0;

  errno = e;
  return -1;
}

#endif


		 /*******************************
		 *	 QUERY CPU TIME		*
		 *******************************/

#define nano * 0.0000001
#define ntick 1.0			/* manual says 100.0 ??? */

double
CpuTime(cputime_kind which)
{ double t;
  HANDLE proc = GetCurrentProcess();
  FILETIME created, exited, kerneltime, usertime;

  if ( GetProcessTimes(proc, &created, &exited, &kerneltime, &usertime) )
  { FILETIME *p;

    switch ( which )
    { case CPU_USER:
	p = &usertime;
	break;
      case CPU_SYSTEM:
	p = &kerneltime;
	break;
      default:
	assert(0);
	return 0.0;
    }
    t = (double)p->dwHighDateTime * (4294967296.0 * ntick nano);
    t += (double)p->dwLowDateTime  * (ntick nano);
  } else				/* '95, Windows 3.1/win32s */
  { t = 0.0;
  }

  return t;
}


int
CpuCount(void)
{ SYSTEM_INFO si;

  GetSystemInfo(&si);

  return si.dwNumberOfProcessors;
}


void
setOSPrologFlags(void)
{ PL_set_prolog_flag("cpu_count", PL_INTEGER, (intptr_t)CpuCount());
#ifdef MSYS2
  PL_set_prolog_flag("msys2", PL_BOOL|FF_READONLY, true);
#endif
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
findExecutable() returns  a Prolog path  for the main executable  or a
module of SWI-Prolog.  Returns NULL if the path cannot be determined.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

char *
findExecutable(const char *module, char *exe, size_t exelen)
{ int n;
  wchar_t wbuf[PATH_MAX];
  HMODULE hmod;

  if ( module )
  { if ( !(hmod = GetModuleHandle(module)) )
    { hmod = GetModuleHandle("libswipl.dll");
      DEBUG(MSG_WIN_API,
	    Sdprintf("Warning: could not find module from \"%s\"\n"
		     "Warning: Trying %s to find home\n",
		     module,
		     hmod ? "\"LIBPL.DLL\"" : "executable"));
    }
  } else
    hmod = NULL;

  if ( (n = GetModuleFileNameW(hmod, wbuf, PATH_MAX)) > 0 )
  { char os_exe[PATH_MAX];
    char *p0;

    wbuf[n] = EOS;
    if ( (p0=_xos_long_file_name_toA(wbuf, os_exe, sizeof(os_exe))) )
      return PrologPath(p0, exe, exelen);
    return NULL;
  } else if ( module )
  { return PrologPath(module, exe, exelen);
  } else
  { return NULL;
  }
}

char *
findModulePath(const char *module, char *buf, size_t len)
{ wchar_t wbuf[PATH_MAX];
  HMODULE hmod;

  if ( (hmod = GetModuleHandle(module)) )
  { int n;

    if ( (n = GetModuleFileNameW(hmod, wbuf, PATH_MAX)) > 0 )
    { wbuf[n] = EOS;
      char osbuf[PATH_MAX];
      char *osp;

      if ( (osp=_xos_long_file_name_toA(wbuf, osbuf, sizeof(osbuf))) )
	return PrologPath(osp, buf, len);
    }
  }

  return NULL;
}


		 /*******************************
		 *     SUPPORT FOR SHELL/2	*
		 *******************************/

typedef struct
{ const char *name;
  UINT        id;
} showtype;

static int
get_showCmd(term_t show, UINT *cmd)
{ char *s;
  showtype *st;
  static showtype types[] =
  { { "hide",		 SW_HIDE },
    { "maximize",	 SW_MAXIMIZE },
    { "minimize",	 SW_MINIMIZE },
    { "restore",	 SW_RESTORE },
    { "show",		 SW_SHOW },
    { "showdefault",	 SW_SHOWDEFAULT },
    { "showmaximized",   SW_SHOWMAXIMIZED },
    { "showminimized",   SW_SHOWMINIMIZED },
    { "showminnoactive", SW_SHOWMINNOACTIVE },
    { "showna",          SW_SHOWNA },
    { "shownoactive",    SW_SHOWNOACTIVATE },
    { "shownormal",      SW_SHOWNORMAL },
					/* compatibility */
    { "normal",		 SW_SHOWNORMAL },
    { "iconic",		 SW_MINIMIZE },
    { NULL, 0 },
  };

  if ( show == 0 )
  { *cmd = SW_SHOWNORMAL;
    succeed;
  }

  if ( !PL_get_chars(show, &s, CVT_ATOM|CVT_EXCEPTION) )
    fail;
  for(st=types; st->name; st++)
  { if ( streq(st->name, s) )
    { *cmd = st->id;
      succeed;
    }
  }

  return PL_error(NULL, 0, NULL, ERR_DOMAIN,
		  PL_new_atom("win_show"), show);
}



static int
win_exec(size_t len, const wchar_t *cmd, UINT show)
{ GET_LD
  STARTUPINFOW startup;
  PROCESS_INFORMATION info;
  int rval;
  wchar_t *wcmd;

  memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  startup.wShowWindow = show;

					/* ensure 0-terminated */
  wcmd = PL_malloc((len+1)*sizeof(wchar_t));
  memcpy(wcmd, cmd, len*sizeof(wchar_t));
  wcmd[len] = 0;

  rval = CreateProcessW(NULL,		/* app */
			wcmd,
			NULL, NULL,	/* security */
			false,		/* inherit handles */
			0,		/* flags */
			NULL,		/* environment */
			NULL,		/* Directory */
			&startup,
			&info);		/* process info */
  PL_free(wcmd);

  if ( rval )
  { CloseHandle(info.hProcess);
    CloseHandle(info.hThread);

    succeed;
  } else
  { term_t tmp = PL_new_term_ref();

    return ( PL_unify_wchars(tmp, PL_ATOM, len, cmd) &&
	     PL_error(NULL, 0, WinError(), ERR_SHELL_FAILED, tmp)
	   );
  }
}


static void
utf8towcs_buffer(Buffer b, const char *src)
{ for( ; *src; )
  { int wc;

    PL_utf8_code_point(&src, NULL, &wc);
    addWcharBuffer(b, wc);
  }

  addWcharBuffer(b, 0);
}


int
System(char *command)			/* command is a UTF-8 string */
{ STARTUPINFOW sinfo;
  PROCESS_INFORMATION pinfo;
  int shell_rval;
  tmp_buffer buf;
  wchar_t *wcmd;

  memset(&sinfo, 0, sizeof(sinfo));
  sinfo.cb = sizeof(sinfo);

  initBuffer(&buf);
  utf8towcs_buffer((Buffer)&buf, command);
  wcmd = baseBuffer(&buf, wchar_t);

  if ( CreateProcessW(NULL,			/* module */
		      wcmd,			/* command line */
		      NULL,			/* Security stuff */
		      NULL,			/* Thread security stuff */
		      false,			/* Inherit handles */
		      CREATE_NO_WINDOW,		/* flags */
		      NULL,			/* environment */
		      NULL,			/* CWD */
		      &sinfo,			/* startup info */
		      &pinfo) )			/* process into */
  { BOOL rval;
    DWORD code;

    CloseHandle(pinfo.hThread);			/* don't need this */
    discardBuffer(&buf);

    do
    { MSG msg;

      if ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
      { TranslateMessage(&msg);
	DispatchMessage(&msg);
      } else
	Sleep(50);

      rval = GetExitCodeProcess(pinfo.hProcess, &code);
    } while(rval == true && code == STILL_ACTIVE);

    shell_rval = (rval == true ? code : -1);
    CloseHandle(pinfo.hProcess);
  } else
  { discardBuffer(&buf);
    return shell_rval = -1;
  }

  return shell_rval;
}


foreign_t
pl_win_exec(term_t cmd, term_t how)
{ wchar_t *s;
  size_t len;
  UINT h;

  if ( PL_get_wchars(cmd, &len, &s, CVT_ALL|CVT_EXCEPTION) &&
       get_showCmd(how, &h) )
  { return win_exec(len, s, h);
  } else
    fail;
}

typedef struct
{ int   eno;
  const char *message;
} shell_error;

static const shell_error se_errors[] =
{ { 0 ,                     "Out of memory or resources" },
  { ERROR_FILE_NOT_FOUND,   "File not found" },
  { ERROR_PATH_NOT_FOUND,   "path not found" },
  { ERROR_BAD_FORMAT,	    "Invalid .EXE" },
  { SE_ERR_ACCESSDENIED,    "Access denied" },
  { SE_ERR_ASSOCINCOMPLETE, "Incomplete association" },
  { SE_ERR_DDEBUSY,	    "DDE server busy" },
  { SE_ERR_DDEFAIL,         "DDE transaction failed" },
  { SE_ERR_DDETIMEOUT,	    "DDE request timed out" },
  { SE_ERR_DLLNOTFOUND,	    "DLL not found" },
  { SE_ERR_FNF,		    "File not found (FNF)" },
  { SE_ERR_NOASSOC,	    "No association" },
  { SE_ERR_OOM,		    "Not enough memory" },
  { SE_ERR_PNF,		    "Path not found (PNF)" },
  { SE_ERR_SHARE,	    "Sharing violation" },
  { 0,			    NULL }
};


static int
win_shell(term_t op, term_t file, term_t how)
{ size_t lo, lf;
  wchar_t *o, *f;
  UINT h;
  HINSTANCE instance;

  if ( !PL_get_wchars(op,   &lo, &o, CVT_ALL|CVT_EXCEPTION|BUF_STACK) ||
       !PL_get_wchars(file, &lf, &f, CVT_ALL|CVT_EXCEPTION|BUF_STACK) ||
       !get_showCmd(how, &h) )
    fail;

  instance = ShellExecuteW(NULL, o, f, NULL, NULL, h);

  if ( (intptr_t)instance <= 32 )
  { const shell_error *se;

    for(se = se_errors; se->message; se++)
      { if ( se->eno == (int)(intptr_t)instance )
	return PL_error(NULL, 0, se->message, ERR_SHELL_FAILED, file);
    }
    PL_error(NULL, 0, NULL, ERR_SHELL_FAILED, file);
  }

  succeed;
}


static
PRED_IMPL("win_shell", 2, win_shell2, 0)
{ return win_shell(A1, A2, 0);
}


static
PRED_IMPL("win_shell", 3, win_shell3, 0)
{ return win_shell(A1, A2, A3);
}


foreign_t
pl_win_module_file(term_t module, term_t file)
{ char buf[PATH_MAX];
  char *m;
  char *f;

  if ( !PL_get_chars(module, &m, CVT_ALL|CVT_EXCEPTION) )
    fail;
  if ( (f = findExecutable(m, buf, sizeof(buf))) )
    return PL_unify_atom_chars(file, f);

  fail;
}

		 /*******************************
		 *	  WINDOWS MESSAGES	*
		 *******************************/

LRESULT
PL_win_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef O_PLMT
  if ( hwnd == NULL &&
       message == WM_SIGNALLED &&
       wParam == 0 &&			/* or another constant? */
       lParam == 0 )
  { if ( PL_handle_signals() < 0 )
      return PL_MSG_EXCEPTION_RAISED;

    return PL_MSG_HANDLED;
  }
#endif

  return PL_MSG_IGNORED;
}


		 /*******************************
		 *	DLOPEN AND FRIENDS	*
		 *******************************/

#ifdef EMULATE_DLOPEN

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
These functions emulate the bits from the ELF shared object interface we
need. They are used  by  pl-load.c,   which  defines  the  actual Prolog
interface.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef HAVE_LIBLOADERAPI_H
#include <LibLoaderAPI.h>
#else
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif
typedef void * DLL_DIRECTORY_COOKIE;
#endif

static const char *dlmsg;
static DLL_DIRECTORY_COOKIE (WINAPI *f_AddDllDirectoryW)(wchar_t* dir);
static BOOL (WINAPI *f_RemoveDllDirectory)(DLL_DIRECTORY_COOKIE);

static DWORD
load_library_search_flags(void)
{ static int done = false;
  static DWORD flags = 0;

  if ( !done )
  { HMODULE kernel = GetModuleHandle(TEXT("kernel32.dll"));

    if ( (f_AddDllDirectoryW   = (void*)GetProcAddress(kernel, "AddDllDirectory")) &&
	 (f_RemoveDllDirectory = (void*)GetProcAddress(kernel, "RemoveDllDirectory")) )
    { flags = ( LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|
		LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
      DEBUG(MSG_WIN_API,
	    Sdprintf("LoadLibraryExW() flags are supported\n"));
    } else
    { DEBUG(MSG_WIN_API,
	    Sdprintf("LoadLibraryExW() flags are NOT supported\n"));
    }
    done = true;
  }

  return flags;
}


static
PRED_IMPL("win_add_dll_directory", 2, win_add_dll_directory, 0)
{ PRED_LD
  char *dirs;

  if ( PL_get_file_name(A1, &dirs, REP_UTF8) )
  { wchar_t dirw[PATH_MAX];
    DLL_DIRECTORY_COOKIE cookie;

    if ( _xos_os_filenameW(dirs, dirw, PATH_MAX) == NULL )
      return PL_representation_error("file_name");
    if ( load_library_search_flags() )
    { int eno;

      /* AddDllDirectoryW() cannot handle "\\?\" */
      if ( (cookie = (*f_AddDllDirectoryW)(dirw + _xos_win_prefix_length(dirw))) )
      { DEBUG(MSG_WIN_API,
	      SdprintfX("AddDllDirectory(%Ws) ok\n", dirw));

	return PL_unify_int64(A2, (int64_t)(uintptr_t)cookie);
      }

      switch((eno=GetLastError()))
      { case ERROR_FILE_NOT_FOUND:
	  return PL_existence_error("directory", A1);
	case ERROR_INVALID_PARAMETER:
	  return PL_domain_error("absolute_file_name", A1);
      }
      return PL_error(NULL, 0, WinErrorNo(eno), ERR_SYSCALL, "AddDllDirectory()");
    } else
      return false;
  } else
    return false;
}


static
PRED_IMPL("win_remove_dll_directory", 1, win_remove_dll_directory, 0)
{ int64_t icookie;

  if ( PL_get_int64_ex(A1, &icookie) )
  { if ( f_RemoveDllDirectory )
    { if ( (*f_RemoveDllDirectory)((DLL_DIRECTORY_COOKIE)(uintptr_t)icookie) )
	return true;

      return PL_error(NULL, 0, WinError(), ERR_SYSCALL, "RemoveDllDirectory()");
    } else
      return false;
  } else
    return false;
}


static int
is_windows_abs_path(const wchar_t *path)
{ if ( path[1] == ':' && path[0] < 0x80 && iswalpha(path[0]) )
    return true;			/* drive */
  if ( path[0] == '\\' && path[1] == '\\' )
    return true;			/* UNC */

  return false;
}

void *
PL_dlopen(const char *file, int flags)	/* file is in UTF-8, POSIX path */
{ HINSTANCE h;
  DWORD llflags = 0;
  wchar_t wfile[PATH_MAX];

  if ( strchr(file, '/') || strchr(file, '\\' ) )
  { if ( _xos_os_filenameW(file, wfile, PATH_MAX) == NULL )
    { dlmsg = "Name too long";
      return NULL;
    }
  } else
  { wchar_t *w = wfile;
    wchar_t *e = &w[PATH_MAX-1];

    for(const char *s = file; *s; )
    { int c;

      s = utf8_get_char(s, &c);
      if ( w+2 >= e )
      { dlmsg = "Name too long";
	return NULL;
      }
      w = put_wchar(w, c);
    }
    *w = 0;
  }

  DEBUG(MSG_WIN_API, SdprintfX("dlopen(%Ws)\n", wfile));

  if ( is_windows_abs_path(wfile) )
    llflags |= load_library_search_flags();

  if ( (h = LoadLibraryExW(wfile, NULL, llflags)) )
  { dlmsg = "No Error";
    return (void *)h;
  }

  dlmsg = WinError();
  return NULL;
}


const char *
PL_dlerror(void)
{ return dlmsg;
}


void *
PL_dlsym(void *handle, char *symbol)
{ void *addr = GetProcAddress(handle, symbol);

  if ( addr )
  { dlmsg = "No Error";
    return addr;
  }

  dlmsg = WinError();
  return NULL;
}


int
PL_dlclose(void *handle)
{ FreeLibrary(handle);

  return 0;
}

#endif /*EMULATE_DLOPEN*/


static
PRED_IMPL("win_process_modules", 1, win_process_modules, 0)
{ PRED_LD
  HANDLE hProcess = GetCurrentProcess();
  HMODULE lphModule[100];
  HMODULE *found=lphModule;
  DWORD cb = sizeof(lphModule);
  DWORD lpcbNeeded;
  int rc;

  for(;;)
  { if ( EnumProcessModules(hProcess, found, cb, &lpcbNeeded) )
    { if ( lpcbNeeded > cb )
      { if ( !(found = malloc(lpcbNeeded)) )
	  return PL_no_memory();
	cb = lpcbNeeded;
      } else
      { term_t tail = PL_copy_term_ref(A1);
	term_t head = PL_new_term_ref();
	int i;

	for(i=0; i<lpcbNeeded/sizeof(HMODULE); i++)
	{ wchar_t name[PATH_MAX];
	  int n;

	  if ( (n=GetModuleFileNameW(found[i], name, PATH_MAX)) > 0 )
	  { name[n] = EOS;
	    char name_utf8[PATH_MAX*2];
	    char pname[PATH_MAX*2];

	    if ( _xos_canonical_filenameW(name, name_utf8, sizeof(name_utf8), XOS_DOWNCASE) &&
		 PrologPath(name_utf8, pname, sizeof(pname)) )
	    { if ( !PL_unify_list(tail, head, tail) ||
		   !PL_unify_chars(head, PL_ATOM|REP_FN, (size_t)-1, pname)  )
	      { rc = false;
		goto out;
	      }
	    } else
	    { rc = PL_representation_error("max_path_length");
	      goto out;
	    }
	  }
	}

	rc = PL_unify_nil(tail);
	break;
      }
    }
  }

out:
  if ( found != lphModule )
    free(found);

  return rc;
}


		 /*******************************
		 *	 SNPRINTF MADNESS	*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MS-Windows _snprintf() may look like C99 snprintf(), but is is not quite
the same: on overflow, the buffer is   *not* 0-terminated and the return
is negative (unspecified how negative).  The   code  below  works around
this, returning count on overflow. This is still not the same as the C99
version that returns the  number  of   characters  that  would have been
written, but it seems to be enough for our purposes.

See http://www.di-mgt.com.au/cprog.html#snprintf

The above came from the provided link, but it is even worse (copied from
VS2005 docs):

  - If len < count, then len characters are stored in buffer, a
  null-terminator is appended, and len is returned.

  - If len = count, then len characters are stored in buffer, no
  null-terminator is appended, and len is returned.

  - If len > count, then count characters are stored in buffer, no
  null-terminator is appended, and a negative value is returned.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
ms_snprintf(char *buffer, size_t count, const char *fmt, ...)
{ va_list ap;
  int ret;

  va_start(ap, fmt);
  ret = _vsnprintf(buffer, count-1, fmt, ap);
  va_end(ap);

  if ( ret < 0 || ret == count )
  { ret = (int)count;
    buffer[count-1] = '\0';
  }

  return ret;
}



		 /*******************************
		 *	      FOLDERS		*
		 *******************************/

#ifdef HAVE_SHLOBJ_H
#include <shlobj.h>
#endif

typedef struct folderid
{ int csidl;
  const char *name;
} folderid;

static const folderid folderids[] =
{ { CSIDL_COMMON_ALTSTARTUP, "common_altstartup" },
  { CSIDL_ALTSTARTUP, "altstartup" },
  { CSIDL_APPDATA, "appdata" },
  { CSIDL_COMMON_APPDATA, "common_appdata" },
  { CSIDL_LOCAL_APPDATA, "local_appdata" },
  { CSIDL_CONTROLS, "controls" },
  { CSIDL_COOKIES, "cookies" },
  { CSIDL_DESKTOP, "desktop" },
  { CSIDL_COMMON_DESKTOPDIRECTORY, "common_desktopdirectory" },
  { CSIDL_DESKTOPDIRECTORY, "desktopdirectory" },
  { CSIDL_COMMON_FAVORITES, "common_favorites" },
  { CSIDL_FAVORITES, "favorites" },
  { CSIDL_FONTS, "fonts" },
  { CSIDL_HISTORY, "history" },
  { CSIDL_INTERNET_CACHE, "internet_cache" },
  { CSIDL_INTERNET, "internet" },
  { CSIDL_DRIVES, "drives" },
  { CSIDL_PERSONAL, "personal" },
  { CSIDL_NETWORK, "network" },
  { CSIDL_NETHOOD, "nethood" },
  { CSIDL_PERSONAL, "personal" },
  { CSIDL_PRINTERS, "printers" },
  { CSIDL_PRINTHOOD, "printhood" },
  { CSIDL_COMMON_PROGRAMS, "common_programs" },
  { CSIDL_PROGRAMS, "programs" },
  { CSIDL_RECENT, "recent" },
  { CSIDL_BITBUCKET, "bitbucket" },
  { CSIDL_SENDTO, "sendto" },
  { CSIDL_COMMON_STARTMENU, "common_startmenu" },
  { CSIDL_STARTMENU, "startmenu" },
  { CSIDL_COMMON_STARTUP, "common_startup" },
  { CSIDL_STARTUP, "startup" },
  { CSIDL_TEMPLATES, "templates" },
  { 0, NULL }
};


static int
unify_csidl_path(term_t t, int csidl)
{ wchar_t buf[PATH_MAX];

  if ( SHGetSpecialFolderPathW(0, buf, csidl, false) )
  { wchar_t *p;

    for(p=buf; *p; p++)
    { if ( *p == '\\' )
	*p = '/';
    }

    return PL_unify_wchars(t, PL_ATOM, -1, buf);
  } else
    return PL_error(NULL, 0, WinError(), ERR_SYSCALL, "SHGetSpecialFolderPath");
}


static
PRED_IMPL("win_folder", 2, win_folder, PL_FA_NONDETERMINISTIC)
{ GET_LD
  int n;

  switch( CTX_CNTRL )
  { case FRG_FIRST_CALL:
      if ( PL_is_variable(A1) )
      { n = 0;
	goto generate;
      } else
      { char *s;

	if ( PL_get_chars(A1, &s, CVT_ATOM|CVT_EXCEPTION) )
	{ const folderid *fid;

	  for(fid = folderids; fid->name; fid++)
	  { if ( streq(s, fid->name) )
	      return unify_csidl_path(A2, fid->csidl);
	  }

	  { atom_t dom = PL_new_atom("win_folder");

	    PL_error(NULL, 0, NULL, ERR_DOMAIN, dom, A1);
	    PL_unregister_atom(dom);
	    return false;
	  }
	} else
	  return false;
      }
    case FRG_REDO:
    { fid_t fid;

      n = (int)CTX_INT+1;

      generate:
	fid = PL_open_foreign_frame();
	for(; folderids[n].name; n++)
	{ if ( unify_csidl_path(A2, folderids[n].csidl) &&
	       PL_unify_atom_chars(A1, folderids[n].name) )
	  { PL_close_foreign_frame(fid);
	    ForeignRedoInt(n);
	  }
	  if ( PL_exception(0) )
	    PL_clear_exception();
	  PL_rewind_foreign_frame(fid);
	}
	PL_close_foreign_frame(fid);
	return false;
    }
    default:
      succeed;
  }
}



		 /*******************************
		 *	      REGISTRY		*
		 *******************************/

#define wstreq(s,q) (wcscmp((s), (q)) == 0)

static HKEY
reg_open_key(const wchar_t *which, int create)
{ HKEY key = HKEY_CURRENT_USER;
  DWORD disp;
  LONG rval;

  while(*which)
  { wchar_t buf[256];
    wchar_t *s;
    HKEY tmp;

    for(s=buf; *which && !(*which == '/' || *which == '\\'); )
      *s++ = *which++;
    *s = '\0';
    if ( *which )
      which++;

    if ( wstreq(buf, L"HKEY_CLASSES_ROOT") )
    { key = HKEY_CLASSES_ROOT;
      continue;
    } else if ( wstreq(buf, L"HKEY_CURRENT_USER") )
    { key = HKEY_CURRENT_USER;
      continue;
    } else if ( wstreq(buf, L"HKEY_LOCAL_MACHINE") )
    { key = HKEY_LOCAL_MACHINE;
      continue;
    } else if ( wstreq(buf, L"HKEY_USERS") )
    { key = HKEY_USERS;
      continue;
    }

    DEBUG(2, Sdprintf("Trying %s\n", buf));
    if ( RegOpenKeyExW(key, buf, 0L, KEY_READ, &tmp) == ERROR_SUCCESS )
    { RegCloseKey(key);
      key = tmp;
      continue;
    }

    if ( !create )
      return NULL;

    rval = RegCreateKeyExW(key, buf, 0, L"", 0,
			  KEY_ALL_ACCESS, NULL, &tmp, &disp);
    RegCloseKey(key);
    if ( rval == ERROR_SUCCESS )
      key = tmp;
    else
      return NULL;
  }

  return key;
}

#define MAXREGSTRLEN 1024

static
PRED_IMPL("win_registry_get_value", 3, win_registry_get_value, 0)
{ GET_LD
  DWORD type;
  union
  { BYTE bytes[MAXREGSTRLEN];
    wchar_t wchars[MAXREGSTRLEN/sizeof(wchar_t)];
    DWORD dword;
  } data;
  DWORD len = sizeof(data);
  size_t klen, namlen;
  wchar_t *k, *name;
  HKEY key;

  term_t Key = A1;
  term_t Name = A2;
  term_t Value = A3;

  if ( !PL_get_wchars(Key, &klen, &k, CVT_ATOM|CVT_EXCEPTION) ||
       !PL_get_wchars(Name, &namlen, &name, CVT_ATOM|CVT_ATOM) )
    return false;
  if ( !(key=reg_open_key(k, false)) )
    return PL_error(NULL, 0, NULL, ERR_EXISTENCE, ATOM_key, Key);

  DEBUG(9, Sdprintf("key = %p, name = %s\n", key, name));
  if ( RegQueryValueExW(key, name, NULL, &type, data.bytes, &len)
							== ERROR_SUCCESS )
  { RegCloseKey(key);

    switch(type)
    { case REG_SZ:
	return PL_unify_wchars(Value, PL_ATOM,
			       len/sizeof(wchar_t)-1, data.wchars);
      case REG_DWORD:
	return PL_unify_integer(Value, data.dword);
      default:
	warning("get_registry_value/2: Unknown registery-type: %d", type);
	fail;
    }
  }

  return false;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Get the local, global,  trail  and   argument-stack  defaults  from  the
registry.  They  can  be  on  the   HKEY_CURRENT_USER  as  well  as  the
HKEY_LOCAL_MACHINE  registries  to  allow   for    both   user-only  and
system-wide settings.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static struct regdef
{ const char *name;
  size_t     *address;
} const regdefs[] =
{ { "stackLimit",   &GD->defaults.stack_limit },
  { "tableSpace",   &GD->defaults.table_space },
  { NULL,           NULL }
};


static void
setStacksFromKey(HKEY key)
{ DWORD type;
  union
  { BYTE bytes[128];
    DWORD dword;
  } data;
  DWORD len = sizeof(data);
  const struct regdef *rd;

  for(rd = regdefs; rd->name; rd++)
  { if ( RegQueryValueEx(key, rd->name, NULL, &type, data.bytes, &len) ==
							ERROR_SUCCESS &&
	 type == REG_DWORD )
    { DWORD v = data.dword;

      *rd->address = (size_t)v;
    }
  }
}


void
getDefaultsFromRegistry(void)
{ HKEY key;

  if ( (key = reg_open_key(L"HKEY_LOCAL_MACHINE/Software/SWI/Prolog", false)) )
  { setStacksFromKey(key);
    RegCloseKey(key);
  }
  if ( (key = reg_open_key(L"HKEY_CURRENT_USER/Software/SWI/Prolog", false)) )
  { setStacksFromKey(key);
    RegCloseKey(key);
  }
}


const char *
PL_w32_running_under_wine(void)
{ static const char * (CDECL *pwine_get_version)(void);
  HMODULE hntdll = GetModuleHandle("ntdll.dll");

  if ( !hntdll )
  { return NULL;
  }

  if ( (pwine_get_version = (void *)GetProcAddress(hntdll, "wine_get_version")) )
    return pwine_get_version();

  return NULL;
}


		 /*******************************
		 *		MUI		*
		 *******************************/


static int
langid_num(const wchar_t *s)
{ int v=0;

  for(; *s; s++)
  { v =	v<<4;
    if ( *s >= '0' && *s <= '9' )
      v += *s-'0';
    else if ( *s >= 'A' && *s <= 'F' )
      v += *s-'A'+10;
    else if ( *s >= 'a' && *s <= 'f' )
      v += *s-'a'+10;
    else
      assert(0);
  }

  return v;
}


static
PRED_IMPL("win_get_user_preferred_ui_languages", 2, win_get_user_preferred_ui_languages, 0)
{ PRED_LD
  char *how;
  DWORD flags;
  ULONG num, sz = 0;
  wchar_t store[1024];
  wchar_t *buf = store;
  int rc = true;

  if ( PL_get_atom_chars(A1, &how) )
  { if ( strcmp(how, "id") == 0 )
      flags = MUI_LANGUAGE_ID;
    else if ( strcmp(how, "name") == 0 )
      flags = MUI_LANGUAGE_NAME;
    else
      return PL_domain_error("format", A1);
  } else
    return PL_type_error("atom", A1);

  if ( (rc=GetUserPreferredUILanguages(flags, &num, NULL, &sz)) )
  { if ( sz > sizeof(store)/sizeof(store[0]) )
    { if ( !(buf = malloc(sz*sizeof(store[0]))) )
	return PL_no_memory();
    }
    if ( (rc=GetUserPreferredUILanguages(flags, &num, buf, &sz)) )
    { term_t tail = PL_copy_term_ref(A2);
      term_t head = PL_new_term_ref();
      wchar_t *s = buf;

      while( rc && *s )
      { if ( (rc=PL_unify_list(tail, head, tail)) )
	{ if ( flags == MUI_LANGUAGE_NAME )
	  { rc = PL_unify_wchars(head, PL_ATOM, (size_t)-1, s);
	  } else
	  { rc = PL_unify_integer(head, langid_num(s));
	  }
	}

	if ( rc )
	{ s += wcslen(s);
	  s ++;
	}
      }
      rc = rc && PL_unify_nil(tail);
    }
  }

  if ( buf && buf != store )
    free(buf);

  return rc;
}





		 /*******************************
		 *      PUBLISH PREDICATES	*
		 *******************************/

BeginPredDefs(win)
  PRED_DEF("win_shell",		       2, win_shell2,		    0)
  PRED_DEF("win_shell",		       3, win_shell3,		    0)
  PRED_DEF("win_registry_get_value",   3, win_registry_get_value,   0)
  PRED_DEF("win_folder",	       2, win_folder,		    PL_FA_NONDETERMINISTIC)
  PRED_DEF("win_add_dll_directory",    2, win_add_dll_directory,    0)
  PRED_DEF("win_remove_dll_directory", 1, win_remove_dll_directory, 0)
  PRED_DEF("win_process_modules",      1, win_process_modules,	    0)
  PRED_DEF("win_get_user_preferred_ui_languages", 2, win_get_user_preferred_ui_languages, 0)
EndPredDefs

#endif /*__WINDOWS__*/
