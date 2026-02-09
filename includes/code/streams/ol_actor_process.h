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
 * - Green thread integration for lightweight concurrency
 * 
 * Process lifecycle:
 * 1. NEW: Created but not started
 * 2. READY: Initialized and ready to run
 * 3. RUNNING: Actively executing
 * 4. SUSPENDED: Waiting for messages
 * 5. DONE/CRASHED/KILLED: Terminal states
 * 
 * @author OverLab Group
 * @version 1.3.0
 * @date 2026
 */

#ifndef OL_PROCESS_H
#define OL_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_actor_arena.h"
#include "ol_green_threads.h"
#include "ol_actor_serialize.h"

/** 
 * @brief Process state enumeration
 * 
 * @details Represents the lifecycle state of a process.
 * Processes transition through these states during their lifetime.
 */
typedef enum {
    OL_PROCESS_NEW,        /**< Created but not started */
    OL_PROCESS_READY,      /**< Ready to run (initialized) */
    OL_PROCESS_RUNNING,    /**< Currently executing */
    OL_PROCESS_SUSPENDED,  /**< Suspended (waiting for message) */
    OL_PROCESS_DONE,       /**< Finished successfully */
    OL_PROCESS_CRASHED,    /**< Crashed with error */
    OL_PROCESS_KILLED      /**< Forcefully terminated */
} ol_process_state_t;

/**
 * @brief Process flags for configuration
 * 
 * @details Flags that modify process behavior and characteristics.
 * Can be combined using bitwise OR.
 */
typedef enum {
    OL_PROCESS_SYSTEM      = 1 << 0, /**< System process (higher priority) */
    OL_PROCESS_TRAP_EXIT   = 1 << 1, /**< Trap exit signals (don't crash on linked process exit) */
    OL_PROCESS_HIDDEN      = 1 << 2, /**< Hidden from process listing */
    OL_PROCESS_HEAP_ONLY   = 1 << 3  /**< Use heap only (no arena isolation) */
} ol_process_flags_t;

/**
 * @brief Process exit reasons
 * 
 * @details Reasons why a process terminated. Used for error handling
 * and supervision decisions.
 */
typedef enum {
    OL_EXIT_NORMAL,        /**< Normal termination (intentional) */
    OL_EXIT_KILL,          /**< Killed by another process */
    OL_EXIT_ERROR,         /**< Error in process execution */
    OL_EXIT_TIMEOUT,       /**< Timeout expiration */
    OL_EXIT_NOPROC         /**< No such process (for monitoring) */
} ol_exit_reason_t;

/**
 * @brief Opaque process structure
 * 
 * @details Actual implementation is hidden. Users interact with processes
 * through the provided API functions.
 */
typedef struct ol_process ol_process_t;

/**
 * @brief Process ID type
 * 
 * @details Unique identifier for each process. Used for message routing
 * and process identification.
 */
typedef uint64_t ol_pid_t;

/**
 * @brief Process entry function signature
 * 
 * @param self Process instance (passed to entry function)
 * @param arg User argument passed during process creation
 * 
 * @note This function runs in the process's execution context.
 *       It should not block indefinitely to allow message processing.
 */
typedef void (*ol_process_entry_fn)(ol_process_t* self, void* arg);

/**
 * @brief Exit handler function signature
 * 
 * @param process Process that is handling the exit (the monitor/linked process)
 * @param from_pid PID of process that exited
 * @param reason Reason for exit
 * @param exit_data Optional exit data (process-specific)
 * 
 * @note Called when a linked or monitored process exits.
 */
typedef void (*ol_exit_handler_fn)(ol_process_t* process, ol_pid_t from_pid, 
                                   ol_exit_reason_t reason, void* exit_data);

/**
 * @brief Create a new process with full isolation
 * 
 * @param entry Entry function to execute in the process
 * @param arg Argument passed to entry function
 * @param parent Parent process (NULL for root/independent process)
 * @param flags Process configuration flags (bitwise OR of ol_process_flags_t)
 * @param arena_size Size of memory arena (0 for default)
 * @return ol_process_t* New process instance, NULL on failure
 * 
 * @details Creates a fully isolated process with:
 * - Unique PID
 * - Private memory arena (unless OL_PROCESS_HEAP_ONLY flag)
 * - Mailbox for message passing
 * - Green thread for execution
 * - Parent-child relationship (if parent specified)
 * 
 * @note The process starts in NEW state. Call ol_process_start() (or equivalent)
 *       to begin execution.
 */
ol_process_t* ol_process_create(ol_process_entry_fn entry, void* arg,
                               ol_process_t* parent, uint32_t flags,
                               size_t arena_size);

/**
 * @brief Destroy a process and all its resources
 * 
 * @param process Process to destroy
 * @param reason Exit reason to report to linked/monitoring processes
 * 
 * @details Performs graceful shutdown and cleanup:
 * 1. Sets exit reason
 * 2. Notifies linked and monitoring processes
 * 3. Frees all allocated resources
 * 4. Removes from process registry
 * 
 * @note If process is still running, it will be terminated.
 *       Linked processes will receive exit notifications.
 */
void ol_process_destroy(ol_process_t* process, ol_exit_reason_t reason);

/**
 * @brief Get process PID (Process ID)
 * 
 * @param process Process instance
 * @return ol_pid_t Process ID, 0 if process is NULL
 * 
 * @note PID 0 is reserved/invalid. Valid PIDs start from 1000.
 */
ol_pid_t ol_process_pid(const ol_process_t* process);

/**
 * @brief Get current process state
 * 
 * @param process Process instance
 * @return ol_process_state_t Current state, OL_PROCESS_KILLED if process is NULL
 */
ol_process_state_t ol_process_state(const ol_process_t* process);

/**
 * @brief Get process exit reason
 * 
 * @param process Process instance
 * @return ol_exit_reason_t Exit reason, OL_EXIT_NOPROC if process is NULL or not terminated
 * 
 * @note Only valid for processes in terminal states (DONE, CRASHED, KILLED).
 */
ol_exit_reason_t ol_process_exit_reason(const ol_process_t* process);

/**
 * @brief Link two processes (bidirectional monitoring)
 * 
 * @param process1 First process to link
 * @param process2 Second process to link
 * @return int OL_SUCCESS on success, OL_ERROR on error
 * 
 * @details Creates a bidirectional link where each process monitors the other.
 * If either process exits, the other will be notified.
 * 
 * @note Links are symmetric. Use ol_process_monitor() for one-way monitoring.
 */
int ol_process_link(ol_process_t* process1, ol_process_t* process2);

/**
 * @brief Monitor a process (one-way monitoring)
 * 
 * @param monitor Process that will monitor the target
 * @param target Process to be monitored
 * @return ol_pid_t Monitor reference ID (0 on error)
 * 
 * @details Sets up one-way monitoring where 'monitor' will be notified
 * when 'target' exits. Returns a reference ID that can be used to
 * remove the monitor with ol_process_unlink().
 * 
 * @note Unlike linking, monitoring is unidirectional.
 */
ol_pid_t ol_process_monitor(ol_process_t* monitor, ol_process_t* target);

/**
 * @brief Unlink processes (remove bidirectional link)
 * 
 * @param process1 First process
 * @param process2 Second process
 * @return int OL_SUCCESS on success, OL_ERROR on error or if not linked
 * 
 * @details Removes bidirectional link between processes.
 * After unlinking, neither process will be notified if the other exits.
 */
int ol_process_unlink(ol_process_t* process1, ol_process_t* process2);

/**
 * @brief Send a message to process
 * 
 * @param process Target process
 * @param data Message data
 * @param size Message size in bytes
 * @param sender_pid Sender PID (0 for anonymous send)
 * @return int OL_SUCCESS on success, OL_ERROR on error
 * 
 * @details Sends a message to the process's mailbox. The message is
 * serialized for inter-process communication if needed.
 * 
 * @note Messages to same-arena processes may use zero-copy optimization.
 */
int ol_process_send(ol_process_t* process, const void* data, size_t size,
                   ol_pid_t sender_pid);

/**
 * @brief Receive a message with timeout
 * 
 * @param process Process to receive for
 * @param out_data Will receive pointer to message data
 * @param out_size Will receive message size
 * @param out_sender Will receive sender PID (optional, can be NULL)
 * @param timeout_ms Timeout in milliseconds:
 *                  - 0: Non-blocking (return immediately)
 *                  - -1: Infinite wait
 *                  - >0: Wait specified milliseconds
 * @return int 1 on success (message received),
 *             0 on timeout (no message),
 *             -1 on error (process dead, etc.)
 * 
 * @note The caller is responsible for freeing the received data with free().
 */
int ol_process_recv(ol_process_t* process, void** out_data, size_t* out_size,
                   ol_pid_t* out_sender, int timeout_ms);

/**
 * @brief Set process exit handler
 * 
 * @param process Process to set handler for
 * @param handler Exit handler function (can be NULL to clear)
 * @param user_data User data passed to handler
 * 
 * @details Sets a callback to be called when a linked or monitored
 * process exits. The handler runs in the context of the monitoring process.
 */
void ol_process_set_exit_handler(ol_process_t* process,
                                ol_exit_handler_fn handler,
                                void* user_data);

/**
 * @brief Get process memory arena
 * 
 * @param process Process instance
 * @return ol_arena_t* Process memory arena, NULL if process is NULL or uses heap
 * 
 * @note Returns NULL if process was created with OL_PROCESS_HEAP_ONLY flag.
 */
ol_arena_t* ol_process_arena(const ol_process_t* process);

/**
 * @brief Get process green thread
 * 
 * @param process Process instance
 * @return ol_gt_t* Green thread handle, NULL if process is NULL
 * 
 * @note Green threads provide lightweight concurrency within the same OS thread.
 */
ol_gt_t* ol_process_green_thread(const ol_process_t* process);

/**
 * @brief Get parent process
 * 
 * @param process Process instance
 * @return ol_process_t* Parent process, NULL if process is NULL or root process
 */
ol_process_t* ol_process_parent(const ol_process_t* process);

/**
 * @brief Check if process is alive
 * 
 * @param process Process instance
 * @return bool true if process is in RUNNING, READY, or SUSPENDED state,
 *              false otherwise or if process is NULL
 * 
 * @note A process is considered alive if it can send/receive messages.
 */
bool ol_process_is_alive(const ol_process_t* process);

/**
 * @brief Crash a process with specific reason
 * 
 * @param process Process to crash
 * @param reason Exit reason
 * @param exit_data Optional exit data (passed to exit handlers)
 * 
 * @details Forces a process to crash with specified reason.
 * Linked and monitoring processes will be notified.
 * 
 * @note This is a controlled crash, useful for testing supervision strategies.
 */
void ol_process_crash(ol_process_t* process, ol_exit_reason_t reason,
                     void* exit_data);

/**
 * @brief Get number of linked processes
 * 
 * @param process Process instance
 * @return size_t Number of linked processes, 0 if process is NULL
 */
size_t ol_process_link_count(const ol_process_t* process);

/**
 * @brief Get number of monitoring processes
 * 
 * @param process Process instance
 * @return size_t Number of processes monitoring this process, 0 if process is NULL
 */
size_t ol_process_monitor_count(const ol_process_t* process);

#endif /* OL_PROCESS_H */
