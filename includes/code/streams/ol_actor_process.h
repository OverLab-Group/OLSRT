/**
 * @file ol_process.h
 * @brief Process management with full isolation and supervision trees
 * 
 * @details Implements process isolation similar to Erlang/OTP BEAM VM.
 * Each process has its own memory arena, execution context, and mailbox.
 * Processes can be linked, monitored, and supervised with configurable strategies.
 * 
 * Key features:
 * - Full memory isolation between processes
 * - Supervision trees with configurable restart strategies
 * - Process linking and monitoring
 * - Allow it to crash philosophy
 * - Automatic restart on failure
 */

#ifndef OL_PROCESS_H
#define OL_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_arena.h"
#include "ol_green_threads.h"
#include "ol_serialize.h"

/** Process state enumeration */
typedef enum {
    OL_PROCESS_NEW,        /**< Created but not started */
    OL_PROCESS_READY,      /**< Ready to run */
    OL_PROCESS_RUNNING,    /**< Currently executing */
    OL_PROCESS_SUSPENDED,  /**< Suspended (waiting for message) */
    OL_PROCESS_DONE,       /**< Finished successfully */
    OL_PROCESS_CRASHED,    /**< Crashed with error */
    OL_PROCESS_KILLED      /**< Forcefully terminated */
} ol_process_state_t;

/** Process flags */
typedef enum {
    OL_PROCESS_SYSTEM      = 1 << 0, /**< System process (high priority) */
    OL_PROCESS_TRAP_EXIT   = 1 << 1, /**< Trap exit signals */
    OL_PROCESS_HIDDEN      = 1 << 2, /**< Hidden from process listing */
    OL_PROCESS_HEAP_ONLY   = 1 << 3  /**< Use heap only (no arena) */
} ol_process_flags_t;

/** Process exit reasons */
typedef enum {
    OL_EXIT_NORMAL,        /**< Normal termination */
    OL_EXIT_KILL,          /**< Killed by another process */
    OL_EXIT_ERROR,         /**< Error in process */
    OL_EXIT_TIMEOUT,       /**< Timeout */
    OL_EXIT_NOPROC         /**< No such process */
} ol_exit_reason_t;

/** Process structure (opaque) */
typedef struct ol_process ol_process_t;

/** Process ID type */
typedef uint64_t ol_pid_t;

/** Process entry function signature */
typedef void (*ol_process_entry_fn)(ol_process_t* self, void* arg);

/** Exit handler signature */
typedef void (*ol_exit_handler_fn)(ol_process_t* process, ol_pid_t from_pid, 
                                   ol_exit_reason_t reason, void* exit_data);

/**
 * @brief Create a new process with full isolation
 * 
 * @param entry Entry function to execute
 * @param arg Argument passed to entry function
 * @param parent Parent process (NULL for root)
 * @param flags Process flags
 * @param arena_size Size of memory arena (0 for default)
 * @return ol_process_t* New process or NULL on failure
 */
ol_process_t* ol_process_create(ol_process_entry_fn entry, void* arg,
                               ol_process_t* parent, uint32_t flags,
                               size_t arena_size);

/**
 * @brief Destroy a process and all its resources
 * 
 * @param process Process to destroy
 * @param reason Exit reason
 */
void ol_process_destroy(ol_process_t* process, ol_exit_reason_t reason);

/**
 * @brief Get process PID
 * 
 * @param process Process instance
 * @return ol_pid_t Process ID
 */
ol_pid_t ol_process_pid(const ol_process_t* process);

/**
 * @brief Get process state
 * 
 * @param process Process instance
 * @return ol_process_state_t Current state
 */
ol_process_state_t ol_process_state(const ol_process_t* process);

/**
 * @brief Get process exit reason
 * 
 * @param process Process instance
 * @return ol_exit_reason_t Exit reason if terminated
 */
ol_exit_reason_t ol_process_exit_reason(const ol_process_t* process);

/**
 * @brief Link two processes (monitor each other)
 * 
 * @param process1 First process
 * @param process2 Second process
 * @return int 0 on success, -1 on error
 */
int ol_process_link(ol_process_t* process1, ol_process_t* process2);

/**
 * @brief Monitor a process (one-way monitoring)
 * 
 * @param monitor Monitoring process
 * @param target Process to monitor
 * @return ol_pid_t Monitor reference ID
 */
ol_pid_t ol_process_monitor(ol_process_t* monitor, ol_process_t* target);

/**
 * @brief Unlink processes
 * 
 * @param process1 First process
 * @param process2 Second process
 * @return int 0 on success, -1 on error
 */
int ol_process_unlink(ol_process_t* process1, ol_process_t* process2);

/**
 * @brief Send a message to process
 * 
 * @param process Target process
 * @param data Message data
 * @param size Message size
 * @param sender_pid Sender PID (0 for anonymous)
 * @return int 0 on success, -1 on error
 */
int ol_process_send(ol_process_t* process, const void* data, size_t size,
                   ol_pid_t sender_pid);

/**
 * @brief Receive a message with timeout
 * 
 * @param process Process to receive for
 * @param out_data Output data pointer
 * @param out_size Output size pointer
 * @param out_sender Output sender PID
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return int 1 on success, 0 on timeout, -1 on error
 */
int ol_process_recv(ol_process_t* process, void** out_data, size_t* out_size,
                   ol_pid_t* out_sender, int timeout_ms);

/**
 * @brief Set process exit handler
 * 
 * @param process Process to set handler for
 * @param handler Exit handler function
 * @param user_data User data passed to handler
 */
void ol_process_set_exit_handler(ol_process_t* process,
                                ol_exit_handler_fn handler,
                                void* user_data);

/**
 * @brief Get process memory arena
 * 
 * @param process Process instance
 * @return ol_arena_t* Memory arena
 */
ol_arena_t* ol_process_arena(const ol_process_t* process);

/**
 * @brief Get process green thread
 * 
 * @param process Process instance
 * @return ol_gt_t* Green thread
 */
ol_gt_t* ol_process_green_thread(const ol_process_t* process);

/**
 * @brief Get parent process
 * 
 * @param process Process instance
 * @return ol_process_t* Parent process
 */
ol_process_t* ol_process_parent(const ol_process_t* process);

/**
 * @brief Check if process is alive
 * 
 * @param process Process instance
 * @return bool True if alive
 */
bool ol_process_is_alive(const ol_process_t* process);

/**
 * @brief Crash a process with specific reason
 * 
 * @param process Process to crash
 * @param reason Exit reason
 * @param exit_data Exit data
 */
void ol_process_crash(ol_process_t* process, ol_exit_reason_t reason,
                     void* exit_data);

/**
 * @brief Get number of linked processes
 * 
 * @param process Process instance
 * @return size_t Number of linked processes
 */
size_t ol_process_link_count(const ol_process_t* process);

/**
 * @brief Get number of monitoring processes
 * 
 * @param process Process instance
 * @return size_t Number of monitors
 */
size_t ol_process_monitor_count(const ol_process_t* process);

#endif /* OL_PROCESS_H */