/**
 * @file ol_actor_process.h
 * @brief Process management with full isolation and supervision trees.
 *
 * @details
 * Implements Erlang/OTP-style process isolation. Each process has its
 * own memory arena, execution context, and mailbox. Processes can be
 * linked, monitored, and supervised with configurable strategies.
 *
 * ## v1.3.2 changes
 *
 * The process structure now carries a `driver_thread` field. On POSIX
 * this is a `pthread_t`; on Windows it is a `HANDLE`. When a process
 * is created, a dedicated driver thread is spawned that calls
 * `ol_gt_run_to_completion()` on the process's green thread. This
 * closes the "actor main loop is not driven" gap that kept LSan
 * disabled in v1.3.1.
 *
 * @author OverLab Group
 * @version 1.3.2
 * @date 2026
 */

#ifndef OL_PROCESS_H
#define OL_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

#include "ol_common.h"
#include "ol_actor_arena.h"
#include "ol_green_threads.h"
#include "ol_actor_serialize.h"

/* ==================== Process State ==================== */

/**
 * @brief Process lifecycle state.
 */
typedef enum {
    OL_PROCESS_NEW,        /**< Created but not started. */
    OL_PROCESS_READY,      /**< Ready to run (initialized). */
    OL_PROCESS_RUNNING,    /**< Currently executing. */
    OL_PROCESS_SUSPENDED,  /**< Suspended (waiting for a message). */
    OL_PROCESS_DONE,       /**< Finished successfully. */
    OL_PROCESS_CRASHED,    /**< Crashed with an error. */
    OL_PROCESS_KILLED      /**< Forcefully terminated. */
} ol_process_state_t;

/**
 * @brief Process configuration flags.
 */
typedef enum {
    OL_PROCESS_SYSTEM      = 1 << 0, /**< System process (higher priority). */
    OL_PROCESS_TRAP_EXIT   = 1 << 1, /**< Trap exit signals. */
    OL_PROCESS_HIDDEN      = 1 << 2, /**< Hidden from process listing. */
    OL_PROCESS_HEAP_ONLY   = 1 << 3  /**< Use heap only (no arena isolation). */
} ol_process_flags_t;

/**
 * @brief Process exit reasons.
 */
typedef enum {
    OL_EXIT_NORMAL,        /**< Normal termination. */
    OL_EXIT_KILL,          /**< Killed by another process. */
    OL_EXIT_ERROR,         /**< Error in process execution. */
    OL_EXIT_TIMEOUT,       /**< Timeout expiration. */
    OL_EXIT_NOPROC         /**< No such process. */
} ol_exit_reason_t;

/* ==================== Opaque Types ==================== */

typedef struct ol_process ol_process_t;
typedef uint64_t ol_pid_t;

/* ==================== Callbacks ==================== */

/**
 * @brief Process entry function.
 *
 * @param self Process instance.
 * @param arg  User argument passed at creation.
 */
typedef void (*ol_process_entry_fn)(ol_process_t* self, void* arg);

/**
 * @brief Exit handler for linked or monitored processes.
 */
typedef void (*ol_exit_handler_fn)(ol_process_t* process,
                                   ol_pid_t from_pid,
                                   ol_exit_reason_t reason,
                                   void* exit_data);

/* ==================== Lifecycle ==================== */

/**
 * @brief Create a new fully isolated process.
 *
 * @details
 * As of v1.3.2, this function also spawns a dedicated OS driver
 * thread that runs the process's green thread to completion. The
 * driver thread is stored in `process->driver_thread` and joined
 * in `ol_process_destroy()`.
 *
 * @param entry      Entry function executed inside the green thread.
 * @param arg        Argument passed to the entry function.
 * @param parent     Parent process (NULL for a root process).
 * @param flags      Process flags (bitwise OR of ol_process_flags_t).
 * @param arena_size Private arena size in bytes (0 for default).
 * @return New process handle, or NULL on failure.
 */
ol_process_t* ol_process_create(ol_process_entry_fn entry, void* arg,
                                ol_process_t* parent, uint32_t flags,
                                size_t arena_size);

/**
 * @brief Destroy a process and all its resources.
 *
 * @details
 * v1.3.2: the process's driver thread is joined before cleanup,
 * ensuring the green thread has fully terminated.
 *
 * @param process Process to destroy.
 * @param reason  Exit reason to report to linked/monitoring processes.
 */
void ol_process_destroy(ol_process_t* process, ol_exit_reason_t reason);

/* ==================== Introspection ==================== */

ol_pid_t            ol_process_pid(const ol_process_t* process);
ol_process_state_t  ol_process_state(const ol_process_t* process);
ol_exit_reason_t    ol_process_exit_reason(const ol_process_t* process);
bool                ol_process_is_alive(const ol_process_t* process);
ol_arena_t*         ol_process_arena(const ol_process_t* process);
ol_gt_t*            ol_process_green_thread(const ol_process_t* process);
ol_process_t*       ol_process_parent(const ol_process_t* process);
size_t              ol_process_link_count(const ol_process_t* process);
size_t              ol_process_monitor_count(const ol_process_t* process);

/* ==================== Links and Monitors ==================== */

int      ol_process_link(ol_process_t* process1, ol_process_t* process2);
ol_pid_t ol_process_monitor(ol_process_t* monitor, ol_process_t* target);
int      ol_process_unlink(ol_process_t* process1, ol_process_t* process2);

/* ==================== Messaging ==================== */

int ol_process_send(ol_process_t* process, const void* data, size_t size,
                    ol_pid_t sender_pid);

int ol_process_recv(ol_process_t* process, void** out_data, size_t* out_size,
                    ol_pid_t* out_sender, int timeout_ms);

/* ==================== Exit Handling ==================== */

void ol_process_set_exit_handler(ol_process_t* process,
                                 ol_exit_handler_fn handler,
                                 void* user_data);
void ol_process_crash(ol_process_t* process, ol_exit_reason_t reason,
                      void* exit_data);

#endif /* OL_PROCESS_H */
