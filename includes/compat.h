#pragma once

/* ============================================================
   Linux to Windows C Compatibility Layer
   - Shims for POSIX APIs on Windows (x86/x64)
   - Safe typedefs, macros, and wrappers
   - Drop-in for cross-platform C projects
   ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   Platform split
   ============================================================ */
#ifdef _WIN32
  /* ---------------- Windows includes ---------------- */
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <io.h>
  #include <fcntl.h>
  #include <process.h>
  #include <sys/types.h>
  #include <errno.h>
  #include <time.h>
  #include <sys/stat.h>

  /* ============================================================
     Types (robust, guarded)
     ============================================================ */
  /* ssize_t: pointer-sized signed integer */
  #ifndef _SSIZE_T_DEFINED
    #if defined(_WIN64)
      typedef __int64 ssize_t;
    #else
      typedef int ssize_t;
    #endif
    #define _SSIZE_T_DEFINED
  #endif

  /* socklen_t: Windows sockets use int */
  #ifndef _SOCKLEN_T_DEFINED
    typedef int socklen_t;
    #define _SOCKLEN_T_DEFINED
  #endif

  /* uid/gid: not native; use unsigned int */
  #ifndef _UID_T_DEFINED
    typedef unsigned int uid_t;
    #define _UID_T_DEFINED
  #endif
  #ifndef _GID_T_DEFINED
    typedef unsigned int gid_t;
    #define _GID_T_DEFINED
  #endif

  /* pid_t: define if truly missing (MinGW may provide) */
  //#ifndef _PID_T_DEFINED
    //typedef int pid_t;
    //#define _PID_T_DEFINED
  //#endif

  /* mode_t: minimal width */
  #ifndef _MODE_T_DEFINED
    typedef unsigned short mode_t;
    #define _MODE_T_DEFINED
  #endif

  /* PATH_MAX fallback */
  #ifndef PATH_MAX
  #define PATH_MAX 260
  #endif

  /* Invalid FD sentinel (use -1 to match POSIX expectations) */
  #ifndef OL_INVALID_FD
  #define OL_INVALID_FD (-1)
  #endif

  /* ============================================================
     File flags (POSIX -> MSVCRT)
     ============================================================ */
  #ifndef O_RDONLY
    #define O_RDONLY   _O_RDONLY
    #define O_WRONLY   _O_WRONLY
    #define O_RDWR     _O_RDWR
    #define O_CREAT    _O_CREAT
    #define O_TRUNC    _O_TRUNC
    #define O_APPEND   _O_APPEND
    #define O_EXCL     _O_EXCL
  #endif
  #ifndef O_NONBLOCK
    /* Not for files; for sockets use ioctlsocket(FIONBIO) */
    #define O_NONBLOCK 0x800
  #endif

  /* ============================================================
     errno mappings (minimal, extend as needed)
     ============================================================ */
  #ifndef EWOULDBLOCK
  #define EWOULDBLOCK WSAEWOULDBLOCK
  #endif
  #ifndef EINPROGRESS
  #define EINPROGRESS WSAEINPROGRESS
  #endif
  #ifndef EAGAIN
  #define EAGAIN WSAEWOULDBLOCK
  #endif

  /* ============================================================
     POSIX-like file ops (prefer wrappers/macros)
     ============================================================ */
  #define open   _open
  #define close  _close
  #define lseek  _lseek
  #define unlink _unlink
  #define dup    _dup
  #define dup2   _dup2
  #define fileno _fileno

  /* read/write: cast size to unsigned */
  #define read(fd,buf,n)   _read((fd),(buf),(unsigned)(n))
  #define write(fd,buf,n)  _write((fd),(buf),(unsigned)(n))

  /* pipe shim: use _pipe (no O_NONBLOCK here) */
  static inline int pipe(int fds[2]) {
    /* Binary mode, 4096 buffer; adjust as needed */
    return _pipe(fds, 4096, _O_BINARY) == 0 ? 0 : -1;
  }

  /* fcntl shim: limited support for O_NONBLOCK on files (no-op) */
  #ifndef F_GETFL
  #define F_GETFL 3
  #endif
  #ifndef F_SETFL
  #define F_SETFL 4
  #endif
  static inline int fcntl(int fd, int cmd, int arg) {
    (void)fd; (void)cmd; (void)arg;
    /* On Windows, only sockets support non-blocking via ioctlsocket.
       For plain CRT file descriptors, we return 0 or O_RDONLY baseline. */
    if (cmd == F_GETFL) return O_RDONLY;
    if (cmd == F_SETFL) return 0;
    errno = ENOSYS; return -1;
  }

  /* ============================================================
     stat helpers (S_IS*)
     ============================================================ */
  #ifndef S_ISDIR
  #define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
  #endif
  #ifndef S_ISREG
  #define S_ISREG(m) (((m) & _S_IFREG) == _S_IFREG)
  #endif

  /* ============================================================
     timespec / timeval / gettimeofday / clock_gettime / nanosleep
     ============================================================ */
  #ifndef _TIMESPEC_DEFINED
  struct timespec { long tv_sec; long tv_nsec; };
  #define _TIMESPEC_DEFINED
  #endif

  #ifndef _TIMEVAL_DEFINED
  struct timeval { long tv_sec; long tv_usec; };
  #define _TIMEVAL_DEFINED
  #endif

  static inline int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    FILETIME ft; ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime; uli.HighPart = ft.dwHighDateTime;
    const uint64_t EPOCH_DIFF = 11644473600000000ULL; /* microseconds */
    uint64_t usec = (uli.QuadPart / 10) - EPOCH_DIFF;
    tv->tv_sec  = (long)(usec / 1000000ULL);
    tv->tv_usec = (long)(usec % 1000000ULL);
    return 0;
  }

  /* CLOCK IDs */
  #ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME 0
  #endif
  #ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC 1
  #endif
  //static inline int clock_gettime(int clk_id, struct timespec *ts) {
    //if (!ts) return -1;
    //if (clk_id == CLOCK_REALTIME) {
      //struct timeval tv;
      //if (gettimeofday(&tv, NULL) != 0) return -1;
      //ts->tv_sec  = tv.tv_sec;
      //ts->tv_nsec = tv.tv_usec * 1000L;
      //return 0;
    //} else if (clk_id == CLOCK_MONOTONIC) {
      //static LARGE_INTEGER freq = {0};
      //LARGE_INTEGER now;
      //if (freq.QuadPart == 0) {
        //if (!QueryPerformanceFrequency(&freq)) return -1;
      //}
      //if (!QueryPerformanceCounter(&now)) return -1;
      /* Convert ticks to timespec */
      //double seconds = (double)now.QuadPart / (double)freq.QuadPart;
      //ts->tv_sec  = (long)seconds;
      //ts->tv_nsec = (long)((seconds - (double)ts->tv_sec) * 1000000000.0);
      //return 0;
    //}
    //errno = EINVAL; return -1;
  //}

  //static inline int nanosleep(const struct timespec *req, struct timespec *rem) {
    //(void)rem;
    //if (!req) { errno = EINVAL; return -1; }
    //DWORD ms = (DWORD)(req->tv_sec * 1000L + req->tv_nsec / 1000000L);
    //Sleep(ms);
    //return 0;
  //}

  /* ============================================================
     dirent shim (FindFirstFileA)
     ============================================================ */
  typedef struct dirent {
    char  d_name[PATH_MAX];
    DWORD d_type; /* DT_DIR=4, DT_REG=8 */
  } dirent;

  typedef struct DIR {
    HANDLE hFind;
    WIN32_FIND_DATAA fdata;
    char pattern[PATH_MAX];
    int first;
  } DIR;

  #ifndef DT_UNKNOWN
  #define DT_UNKNOWN 0
  #define DT_REG     8
  #define DT_DIR     4
  #endif

  static inline DIR* opendir(const char *path) {
    if (!path) return NULL;
    DIR *d = (DIR*)calloc(1, sizeof(DIR));
    if (!d) return NULL;
    snprintf(d->pattern, sizeof(d->pattern), "%s\\*", path);
    d->hFind = FindFirstFileA(d->pattern, &d->fdata);
    if (d->hFind == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1; return d;
  }

  static inline struct dirent* readdir(DIR *d) {
    static struct dirent e;
    if (!d) return NULL;
    if (d->first) d->first = 0;
    else if (!FindNextFileA(d->hFind, &d->fdata)) return NULL;
    strncpy(e.d_name, d->fdata.cFileName, sizeof(e.d_name)-1);
    e.d_name[sizeof(e.d_name)-1] = '\0';
    e.d_type = (d->fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &e;
  }

  static inline int closedir(DIR *d) {
    if (!d) return -1;
    if (d->hFind && d->hFind != INVALID_HANDLE_VALUE) FindClose(d->hFind);
    free(d);
    return 0;
  }

  /* ============================================================
     pthreads shim (WinAPI)
     ============================================================ */
  typedef HANDLE             pthread_t;
  typedef CRITICAL_SECTION   pthread_mutex_t;
  typedef int                pthread_mutexattr_t;
  typedef CONDITION_VARIABLE pthread_cond_t;
  typedef int                pthread_condattr_t;

  static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a){(void)a;InitializeCriticalSection(m);return 0;}
  static inline int pthread_mutex_destroy(pthread_mutex_t *m){DeleteCriticalSection(m);return 0;}
  static inline int pthread_mutex_lock(pthread_mutex_t *m){EnterCriticalSection(m);return 0;}
  static inline int pthread_mutex_unlock(pthread_mutex_t *m){LeaveCriticalSection(m);return 0;}

  static inline int pthread_cond_init(pthread_cond_t *c,const pthread_condattr_t*a){(void)a;InitializeConditionVariable(c);return 0;}
  static inline int pthread_cond_wait(pthread_cond_t *c,pthread_mutex_t *m){return SleepConditionVariableCS(c,m,INFINITE)?0:-1;}
  static inline int pthread_cond_timedwait(pthread_cond_t *c,pthread_mutex_t *m,const struct timespec *ts){
    DWORD ms = INFINITE;
    if (ts) ms = (DWORD)(ts->tv_sec * 1000L + ts->tv_nsec / 1000000L);
    return SleepConditionVariableCS(c,m,ms)?0:-1;
  }
  static inline int pthread_cond_signal(pthread_cond_t *c){WakeConditionVariable(c);return 0;}
  static inline int pthread_cond_broadcast(pthread_cond_t *c){WakeAllConditionVariable(c);return 0;}

  typedef unsigned (__stdcall *pthread_start_routine)(void *);
  static inline int pthread_create(pthread_t *t, void *attr, pthread_start_routine start, void *arg){
    (void)attr;
    uintptr_t h = _beginthreadex(NULL, 0, start, arg, 0, NULL);
    if (!h) return -1; *t = (HANDLE)h; return 0;
  }
  static inline int pthread_join(pthread_t t, void **ret){(void)ret;WaitForSingleObject(t,INFINITE);CloseHandle(t);return 0;}

  /* ============================================================
     Networking helpers (Winsock)
     ============================================================ */
  static inline int net_init(void) {
    WSADATA wsa; return (WSAStartup(MAKEWORD(2,2), &wsa) == 0) ? 0 : -1;
  }
  static inline void net_cleanup(void) { WSACleanup(); }

  /* Set non-blocking (socket only) */
  static inline int set_nonblocking_socket(SOCKET s, int nb) {
    u_long mode = nb ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
  }

  /* POSIX-like strcasecmp */
  #ifndef strcasecmp
  #define strcasecmp _stricmp
  #endif
  #ifndef strncasecmp
  #define strncasecmp _strnicmp
  #endif

  /* snprintf fallback (bind to vsnprintf) */
  #if !defined(snprintf)
  static inline int compat_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
  }
  #define snprintf compat_snprintf
  #endif

#else /* ============================ POSIX ============================ */

  /* POSIX includes */
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <dirent.h>
  #include <pthread.h>
  #include <sys/stat.h>
  #include <errno.h>
  #include <time.h>
  #include <sys/time.h>

  /* Networking init/cleanup no-ops on POSIX */
  static inline int  net_init(void) { return 0; }
  static inline void net_cleanup(void) { }

  /* Non-blocking for files/sockets via fcntl */
  static inline int set_nonblocking_fd(int fd, int nb) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    flags = nb ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd, F_SETFL, flags) == 0 ? 0 : -1;
  }

#endif /* platform split */

/* ============================================================
   Common helpers
   ============================================================ */

/* Safe free */
static inline void safe_free(void *p) { if (p) free(p); }

/* Zero memory */
static inline void bzero_safe(void *p, size_t n) { if (p && n) memset(p, 0, n); }

/* Min/Max */
#ifndef OL_MIN
#define OL_MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef OL_MAX
#define OL_MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

/* String duplication fallback (Windows has _strdup; map to strdup) */
#if defined(_WIN32) && !defined(strdup)
  #define strdup _strdup
#endif

/* inet_pton fallback (legacy Windows): prefer ws2tcpip.h implementation.
   If your toolchain lacks inet_pton, you can add a converter here. */

/* send/recv wrappers for ssize_t returns (optional) */
static inline ssize_t send_all(int fd, const void *buf, size_t n) {
#ifdef _WIN32
  int rc = send(fd, (const char*)buf, (int)n, 0);
  return (rc >= 0) ? (ssize_t)rc : -1;
#else
  ssize_t rc = send(fd, buf, n, 0);
  return rc;
#endif
}
static inline ssize_t recv_some(int fd, void *buf, size_t n) {
#ifdef _WIN32
  int rc = recv(fd, (char*)buf, (int)n, 0);
  return (rc >= 0) ? (ssize_t)rc : -1;
#else
  ssize_t rc = recv(fd, buf, n, 0);
  return rc;
#endif
}
