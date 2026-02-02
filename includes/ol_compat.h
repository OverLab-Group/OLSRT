/**
 * @file ol_platform.h
 * @brief Cross-platform OS API abstraction layer
 * @version 1.2.0
 * 
 * @details
 * This header provides cross-platform aliases for Linux-specific system APIs
 * on Windows, macOS, and BSD systems. It maps Linux system calls to their
 * equivalent APIs on other platforms.
 * 
 * @warning This file should be included before any system headers
 */

#ifndef OL_PLATFORM_H
#define OL_PLATFORM_H

/* ============================================================================
 * Platform Detection
 * ============================================================================ */

#if defined(__linux__) && !defined(__ANDROID__)
    #define OL_PLATFORM_LINUX 1
    #define OL_PLATFORM_WINDOWS 0
    #define OL_PLATFORM_MACOS 0
    #define OL_PLATFORM_BSD 0
#elif defined(_WIN32) || defined(_WIN64)
    #define OL_PLATFORM_LINUX 0
    #define OL_PLATFORM_WINDOWS 1
    #define OL_PLATFORM_MACOS 0
    #define OL_PLATFORM_BSD 0
#elif defined(__APPLE__) && defined(__MACH__)
    #define OL_PLATFORM_LINUX 0
    #define OL_PLATFORM_WINDOWS 0
    #define OL_PLATFORM_MACOS 1
    #define OL_PLATFORM_BSD 0
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
      defined(__DragonFly__)
    #define OL_PLATFORM_LINUX 0
    #define OL_PLATFORM_WINDOWS 0
    #define OL_PLATFORM_MACOS 0
    #define OL_PLATFORM_BSD 1
#else
    #error "Unsupported platform"
#endif

/* ============================================================================
 * POSIX Compatibility Layer (All platforms)
 * ============================================================================ */

#if OL_PLATFORM_WINDOWS
    /* Windows requires POSIX compatibility headers */
    #include <pthread.h>
    
    /* Windows POSIX emulation layer */
    #ifndef _POSIX_THREAD_SAFE_FUNCTIONS
        #define _POSIX_THREAD_SAFE_FUNCTIONS 200809L
    #endif
    
    /* Windows doesn't have native POSIX semaphores, use Win32 API */
    #include <windows.h>
    #include <semaphore.h>
    
    /* Windows socket headers */
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    /* Windows time headers */
    #include <time.h>
    
    /* Windows file I/O */
    #include <io.h>
    #include <fcntl.h>
    
    /* Windows memory mapping */
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <sys/mman.h>
    
    /* Windows backtrace support */
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
    
#elif OL_PLATFORM_MACOS
    /* macOS POSIX headers */
    #include <pthread.h>
    #include <semaphore.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/time.h>
    #include <sys/event.h>
    #include <mach/mach_time.h>
    #include <dlfcn.h>
    #include <execinfo.h>
    
#elif OL_PLATFORM_BSD
    /* BSD POSIX headers */
    #include <pthread.h>
    #include <semaphore.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/time.h>
    #include <sys/event.h>
    #include <dlfcn.h>
    #include <execinfo.h>
    
#else /* Linux */
    /* Linux native POSIX headers */
    #include <pthread.h>
    #include <semaphore.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/time.h>
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <sys/timerfd.h>
    #include <execinfo.h>
    #include <dlfcn.h>
#endif

/* ============================================================================
 * Threading API Aliases
 * 
 * These macros provide cross-platform aliases for Linux threading APIs
 * ============================================================================ */

/**
 * @def pthread_create
 * @brief Create a new thread (cross-platform)
 * @details Linux: pthread_create, Windows: _beginthreadex, MacOS/BSD: pthread_create
 */
#if OL_PLATFORM_WINDOWS
    #include <process.h>
    #define pthread_create(thread, attr, start_routine, arg) \
        ((*(thread) = (pthread_t)_beginthreadex(NULL, 0, \
        (unsigned (__stdcall *)(void *))(start_routine), arg, 0, NULL)) == 0 ? -1 : 0)
#else
    /* MacOS/BSD/Linux use standard pthread_create */
#endif

/**
 * @def pthread_join
 * @brief Wait for thread termination (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #define pthread_join(thread, retval) \
        (WaitForSingleObject((HANDLE)(thread), INFINITE) == WAIT_OBJECT_0 ? 0 : -1)
#else
    /* MacOS/BSD/Linux use standard pthread_join */
#endif

/**
 * @def pthread_detach
 * @brief Detach a thread (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #define pthread_detach(thread) \
        (CloseHandle((HANDLE)(thread)) ? 0 : -1)
#else
    /* MacOS/BSD/Linux use standard pthread_detach */
#endif

/**
 * @def pthread_cancel
 * @brief Cancel a thread (cross-platform)
 * @note Windows doesn't support pthread_cancel, use TerminateThread instead
 */
#if OL_PLATFORM_WINDOWS
    #define pthread_cancel(thread) \
        (TerminateThread((HANDLE)(thread), 0) ? 0 : -1)
#else
    /* MacOS/BSD/Linux use standard pthread_cancel */
#endif

/* ============================================================================
 * Mutex API Aliases
 * ============================================================================ */

/**
 * @def PTHREAD_MUTEX_INITIALIZER
 * @brief Static mutex initializer (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef PTHREAD_MUTEX_INITIALIZER
    #define PTHREAD_MUTEX_INITIALIZER {0}
#else
    /* MacOS/BSD/Linux use standard PTHREAD_MUTEX_INITIALIZER */
#endif

/* ============================================================================
 * Semaphore API Aliases
 * 
 * Note: Windows semaphores have different semantics
 * ============================================================================ */

/**
 * @def sem_init
 * @brief Initialize an unnamed semaphore (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_init
    #define sem_init(sem, pshared, value) \
        (((*(sem) = CreateSemaphore(NULL, (value), LONG_MAX, NULL)) != NULL) ? 0 : -1)
#endif

/**
 * @def sem_destroy
 * @brief Destroy an unnamed semaphore (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_destroy
    #define sem_destroy(sem) \
        (CloseHandle(*(sem)) ? 0 : -1)
#endif

/**
 * @def sem_wait
 * @brief Lock a semaphore (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_wait
    #define sem_wait(sem) \
        (WaitForSingleObject(*(sem), INFINITE) == WAIT_OBJECT_0 ? 0 : -1)
#endif

/**
 * @def sem_trywait
 * @brief Try to lock a semaphore (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_trywait
    #define sem_trywait(sem) \
        (WaitForSingleObject(*(sem), 0) == WAIT_OBJECT_0 ? 0 : -1)
#endif

/**
 * @def sem_post
 * @brief Unlock a semaphore (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_post
    #define sem_post(sem) \
        (ReleaseSemaphore(*(sem), 1, NULL) ? 0 : -1)
#endif

/**
 * @def sem_getvalue
 * @brief Get semaphore value (cross-platform)
 * @note Windows doesn't support getting semaphore value directly
 */
#if OL_PLATFORM_WINDOWS
    #undef sem_getvalue
    #define sem_getvalue(sem, sval) \
        (*(sval) = 0, -1) /* Not supported on Windows */
#endif

/* ============================================================================
 * File I/O API Aliases
 * ============================================================================ */

/**
 * @def pipe
 * @brief Create a pipe (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef pipe
    #define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif

/**
 * @def fcntl
 * @brief File control operations (cross-platform)
 * @note Windows has limited fcntl support
 */
#if OL_PLATFORM_WINDOWS
    #ifndef F_GETFL
        #define F_GETFL 0
    #endif
    #ifndef F_SETFL
        #define F_SETFL 0
    #endif
    #ifndef O_NONBLOCK
        #define O_NONBLOCK 0
    #endif
    #define fcntl(fd, cmd, ...) _fcntl(fd, cmd, ##__VA_ARGS__)
#endif

/* ============================================================================
 * Socket API Aliases
 * ============================================================================ */

/**
 * @def socket
 * @brief Create a socket (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef socket
    #define socket(domain, type, protocol) \
        WSASocket(domain, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED)
#endif

/**
 * @def close (for sockets)
 * @brief Close a socket (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #define sock_close(sock) closesocket(sock)
#else
    #define sock_close(sock) close(sock)
#endif

/**
 * @def send/recv
 * @brief Socket send/receive (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    /* Windows uses standard send/recv but with different error handling */
    #define SOCKET_ERROR (-1)
    #define EWOULDBLOCK WSAEWOULDBLOCK
    #define EINPROGRESS WSAEINPROGRESS
#endif

/* ============================================================================
 * Time API Aliases
 * ============================================================================ */

/**
 * @def CLOCK_MONOTONIC
 * @brief Monotonic clock identifier (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #ifndef CLOCK_MONOTONIC
        #define CLOCK_MONOTONIC 1
    #endif
#elif OL_PLATFORM_MACOS
    /* macOS doesn't support CLOCK_MONOTONIC in clock_gettime */
    #ifndef CLOCK_MONOTONIC
        #define CLOCK_MONOTONIC 0
    #endif
#endif

/**
 * @def clock_gettime
 * @brief Get clock time (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef clock_gettime
    #define clock_gettime(clock_id, tp) \
        do { \
            LARGE_INTEGER frequency, counter; \
            QueryPerformanceFrequency(&frequency); \
            QueryPerformanceCounter(&counter); \
            (tp)->tv_sec = counter.QuadPart / frequency.QuadPart; \
            (tp)->tv_nsec = ((counter.QuadPart % frequency.QuadPart) * 1000000000LL) / frequency.QuadPart; \
        } while(0)
#elif OL_PLATFORM_MACOS
    /* macOS implementation of clock_gettime for CLOCK_MONOTONIC */
    #undef clock_gettime
    #define clock_gettime(clock_id, tp) \
        do { \
            struct timeval tv; \
            gettimeofday(&tv, NULL); \
            (tp)->tv_sec = tv.tv_sec; \
            (tp)->tv_nsec = tv.tv_usec * 1000; \
        } while(0)
#endif

/**
 * @def nanosleep
 * @brief High-resolution sleep (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef nanosleep
    #define nanosleep(req, rem) \
        (Sleep(((req)->tv_sec * 1000) + ((req)->tv_nsec / 1000000)), 0)
#endif

/**
 * @def usleep
 * @brief Microsecond sleep (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef usleep
    #define usleep(usec) Sleep((usec) / 1000)
#endif

/* ============================================================================
 * Memory Management API Aliases
 * ============================================================================ */

/**
 * @def mmap
 * @brief Memory mapping (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef mmap
    #define mmap(addr, length, prot, flags, fd, offset) \
        VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    
    #undef munmap
    #define munmap(addr, length) \
        VirtualFree(addr, 0, MEM_RELEASE)
    
    #undef mprotect
    #define mprotect(addr, length, prot) \
        VirtualProtect(addr, length, \
            ((prot) & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY, NULL)
#endif

/* ============================================================================
 * Polling API Aliases
 * 
 * Note: Different platforms use different polling mechanisms
 * ============================================================================ */

/**
 * @def epoll family (Linux) -> kqueue (MacOS/BSD) or WSAPoll (Windows)
 */
#if OL_PLATFORM_WINDOWS
    /* Windows uses WSAPoll */
    #define EPOLLIN  0x001
    #define EPOLLOUT 0x004
    #define EPOLLERR 0x008
    #define EPOLLHUP 0x010
    
    typedef struct epoll_event {
        uint32_t events;
        void* data;
    } epoll_event;
    
    #define epoll_create1(flags) (-1) /* Not implemented, use WSAPoll */
    #define epoll_ctl(epfd, op, fd, event) (-1)
    #define epoll_wait(epfd, events, maxevents, timeout) (-1)
    
#elif OL_PLATFORM_MACOS || OL_PLATFORM_BSD
    /* macOS/BSD use kqueue */
    #define EPOLLIN  EVFILT_READ
    #define EPOLLOUT EVFILT_WRITE
    #define EPOLLERR EV_ERROR
    
    typedef struct epoll_event {
        uint32_t events;
        void* data;
    } epoll_event;
    
    #define epoll_create1(flags) kqueue()
    #define epoll_ctl(kq, op, fd, event) \
        ({ \
            struct kevent change; \
            if ((op) == EPOLL_CTL_ADD) { \
                EV_SET(&change, (fd), (event)->events, EV_ADD, 0, 0, (event)->data); \
            } else if ((op) == EPOLL_CTL_DEL) { \
                EV_SET(&change, (fd), (event)->events, EV_DELETE, 0, 0, NULL); \
            } else if ((op) == EPOLL_CTL_MOD) { \
                EV_SET(&change, (fd), (event)->events, EV_ADD | EV_ENABLE, 0, 0, (event)->data); \
            } \
            kevent((kq), &change, 1, NULL, 0, NULL); \
        })
    #define epoll_wait(kq, events, maxevents, timeout) \
        kevent((kq), NULL, 0, (struct kevent*)(events), (maxevents), \
            (timeout) >= 0 ? &(struct timespec){ (timeout)/1000, ((timeout)%1000)*1000000 } : NULL)
#endif

/* ============================================================================
 * Signal API Aliases
 * ============================================================================ */

#if OL_PLATFORM_WINDOWS
    /* Windows signal handling */
    #define SIGINT  CTRL_C_EVENT
    #define SIGTERM CTRL_BREAK_EVENT
    
    #define sigaction(sig, act, oldact) \
        SetConsoleCtrlHandler(NULL, (act)->sa_handler ? TRUE : FALSE)
#endif

/* ============================================================================
 * Backtrace API Aliases
 * ============================================================================ */

#if OL_PLATFORM_WINDOWS
    /* Windows backtrace */
    #undef backtrace
    #define backtrace(buffer, size) \
        CaptureStackBackTrace(0, (size), (buffer), NULL)
    
    #undef backtrace_symbols
    #define backtrace_symbols(buffer, size) \
        ({ \
            char** symbols = malloc((size) * sizeof(char*)); \
            for (int i = 0; i < (size); i++) { \
                symbols[i] = malloc(256); \
                snprintf(symbols[i], 256, "[0x%p]", (buffer)[i]); \
            } \
            symbols; \
        })
#endif

/* ============================================================================
 * System Information API Aliases
 * ============================================================================ */

/**
 * @def sysconf
 * @brief System configuration (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef sysconf
    #define sysconf(name) \
        ((name) == _SC_PAGESIZE ? 4096 : -1) /* Default 4KB page size */
#endif

/**
 * @def getpagesize
 * @brief Get system page size (cross-platform)
 */
#if OL_PLATFORM_WINDOWS
    #undef getpagesize
    #define getpagesize() 4096
#endif

/* ============================================================================
 * Miscellaneous API Aliases
 * ============================================================================ */

#if OL_PLATFORM_WINDOWS
    /* Windows doesn't have eventfd/timerfd */
    #define eventfd(initval, flags) (-1)
    #define timerfd_create(clockid, flags) (-1)
    #define timerfd_settime(fd, flags, new_value, old_value) (-1)
#endif

/* ============================================================================
 * Error Code Aliases
 * ============================================================================ */

#if OL_PLATFORM_WINDOWS
    /* Map Windows error codes to POSIX */
    #define EAGAIN      WSAEWOULDBLOCK
    #define EINTR       WSAEINTR
    #define EBADF       WSAEBADF
    #define EACCES      WSAEACCES
    #define EFAULT      WSAEFAULT
    #define EINVAL      WSAEINVAL
    #define EMFILE      WSAEMFILE
    #define EWOULDBLOCK WSAEWOULDBLOCK
    #define EINPROGRESS WSAEINPROGRESS
    #define EALREADY    WSAEALREADY
    #define ENOTSOCK    WSAENOTSOCK
    #define EDESTADDRREQ WSAEDESTADDRREQ
    #define EMSGSIZE    WSAEMSGSIZE
    #define EPROTOTYPE  WSAEPROTOTYPE
    #define ENOPROTOOPT WSAENOPROTOOPT
    #define EPROTONOSUPPORT WSAEPROTONOSUPPORT
    #define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
    #define EOPNOTSUPP  WSAEOPNOTSUPP
    #define EPFNOSUPPORT WSAEPFNOSUPPORT
    #define EAFNOSUPPORT WSAEAFNOSUPPORT
    #define EADDRINUSE  WSAEADDRINUSE
    #define EADDRNOTAVAIL WSAEADDRNOTAVAIL
    #define ENETDOWN    WSAENETDOWN
    #define ENETUNREACH WSAENETUNREACH
    #define ENETRESET   WSAENETRESET
    #define ECONNABORTED WSAECONNABORTED
    #define ECONNRESET  WSAECONNRESET
    #define ENOBUFS     WSAENOBUFS
    #define EISCONN     WSAEISCONN
    #define ENOTCONN    WSAENOTCONN
    #define ESHUTDOWN   WSAESHUTDOWN
    #define ETOOMANYREFS WSAETOOMANYREFS
    #define ETIMEDOUT   WSAETIMEDOUT
    #define ECONNREFUSED WSAECONNREFUSED
    #define ELOOP       WSAELOOP
    #define ENAMETOOLONG WSAENAMETOOLONG
    #define EHOSTDOWN   WSAEHOSTDOWN
    #define EHOSTUNREACH WSAEHOSTUNREACH
    #define ENOTEMPTY   WSAENOTEMPTY
#endif

#endif /* OL_PLATFORM_H */