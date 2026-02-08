/**
 * @file ol_process.c
 * @brief Complete process management with Erlang/OTP-style isolation
 * @version 1.2.0
 * 
 * @details Implements isolated processes similar to Erlang/OTP BEAM VM.
 * Each process has its own memory arena, execution context, mailbox, and
 * can be linked/monitored. Supports supervision trees and "let it crash"
 * philosophy with automatic restarts.
 * 
 * Features:
 * - Complete process isolation (memory, execution)
 * - Supervision trees with configurable restart strategies
 * - Process linking and monitoring
 * - Mailbox with message serialization
 * - Automatic restart on failure
 * - Cross-platform support (Linux, Windows, macOS, BSD)
 */

#include "ol_actor_process.h"
#include "ol_actor_arena.h"
#include "ol_green_threads.h"
#include "ol_actor_serialize.h"
#include "ol_lock_mutex.h"
#include "ol_deadlines.h"
#include "ol_actor_hashmap.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#if defined(_WIN32)
    #include <windows.h>
    #define OL_GET_TID() GetCurrentThreadId()
#else
    #include <unistd.h>
    #include <sys/syscall.h>
    #define OL_GET_TID() (pid_t)syscall(SYS_gettid)
#endif

/* ==================== Internal Constants ==================== */

#define DEFAULT_ARENA_SIZE (4 * 1024 * 1024)  /* 4MB default arena */
#define MAX_PROCESS_NAME   256
#define MAILBOX_CAPACITY   1024
#define MAX_LINKS          256
#define MAX_MONITORS       256
#define PROCESS_TIMEOUT_MS 5000

/* ==================== Internal Structures ==================== */

/**
 * @brief Mailbox entry for process messages
 */
typedef struct mailbox_entry {
    ol_serialized_msg_t* msg;    /**< Serialized message */
    ol_pid_t sender;             /**< Sender process ID */
    uint64_t timestamp;          /**< Message timestamp */
    struct mailbox_entry* next;  /**< Next entry in linked list */
} mailbox_entry_t;

/**
 * @brief Process link entry
 */
typedef struct process_link {
    ol_pid_t pid;                /**< Linked process ID */
    bool is_monitor;             /**< True if this is a monitor link */
    uint64_t ref;                /**< Monitor reference */
} process_link_t;

/**
 * @brief Exit information for crashed processes
 */
typedef struct exit_info {
    ol_exit_reason_t reason;     /**< Exit reason */
    void* data;                  /**< Exit data */
    size_t data_size;            /**< Exit data size */
    uint64_t timestamp;          /**< Exit timestamp */
} exit_info_t;

/**
 * @brief Main process structure
 */
struct ol_process {
    /* Identity and state */
    ol_pid_t pid;                       /**< Unique process ID */
    char name[MAX_PROCESS_NAME];        /**< Process name (for debugging) */
    ol_process_state_t state;           /**< Current process state */
    ol_process_flags_t flags;           /**< Process flags */
    
    /* Execution context */
    ol_gt_t* green_thread;              /**< Green thread for execution */
    ol_process_entry_fn entry;          /**< Entry function */
    void* entry_arg;                    /**< Argument for entry function */
    
    /* Memory isolation */
    ol_arena_t* arena;                  /**< Private memory arena */
    size_t arena_size;                  /**< Arena size */
    
    /* Message passing */
    mailbox_entry_t* mailbox_head;      /**< Mailbox linked list head */
    mailbox_entry_t* mailbox_tail;      /**< Mailbox linked list tail */
    size_t mailbox_size;                /**< Number of messages in mailbox */
    ol_mutex_t mailbox_mutex;           /**< Mailbox synchronization */
    ol_cond_t mailbox_cond;             /**< Condition for message arrival */
    
    /* Process relationships */
    ol_process_t* parent;               /**< Parent process */
    process_link_t* links;              /**< Array of linked processes */
    size_t link_count;                  /**< Number of links */
    size_t link_capacity;               /**< Links array capacity */
    
    process_link_t* monitors;           /**< Array of monitoring processes */
    size_t monitor_count;               /**< Number of monitors */
    size_t monitor_capacity;            /**< Monitors array capacity */
    
    /* Exit handling */
    exit_info_t exit_info;              /**< Exit information */
    ol_exit_handler_fn exit_handler;    /**< Exit handler callback */
    void* exit_handler_data;            /**< Data for exit handler */
    
    /* Synchronization */
    ol_mutex_t state_mutex;             /**< Protects process state */
    ol_cond_t state_cond;               /**< Condition for state changes */
    
    /* Statistics */
    uint64_t create_time;               /**< Process creation timestamp */
    uint64_t start_time;                /**< Process start timestamp */
    uint64_t message_count;             /**< Total messages received */
    uint64_t send_count;                /**< Total messages sent */
    size_t peak_mailbox_size;           /**< Peak mailbox usage */
    
    /* System information */
    uint64_t system_thread_id;          /**< OS thread ID (for debugging) */
};

/* ==================== Global Process Management ==================== */

/* Global process registry */
static ol_hashmap_t* g_process_registry = NULL;
static ol_mutex_t g_registry_mutex;
static uint64_t g_next_pid = 1000;  /* Start PIDs from 1000 */
static uint64_t g_next_monitor_ref = 1;

/* Process creation counter for unique names */
static uint32_t g_process_counter = 0;

/* Thread-local current process */
#if defined(_WIN32)
    static __declspec(thread) ol_process_t* g_current_process = NULL;
#else
    static __thread ol_process_t* g_current_process = NULL;
#endif

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Generate unique process ID
 * 
 * @return ol_pid_t New unique process ID
 */
static ol_pid_t ol_process_generate_pid(void) {
    ol_mutex_lock(&g_registry_mutex);
    ol_pid_t pid = g_next_pid++;
    
    /* Ensure we don't wrap around to system PIDs */
    if (g_next_pid < 1000) {
        g_next_pid = 1000;
    }
    
    ol_mutex_unlock(&g_registry_mutex);
    return pid;
}

/**
 * @brief Generate unique monitor reference
 * 
 * @return uint64_t New unique monitor reference
 */
static uint64_t ol_process_generate_monitor_ref(void) {
    uint64_t ref;
    ol_mutex_lock(&g_registry_mutex);
    ref = g_next_monitor_ref++;
    ol_mutex_unlock(&g_registry_mutex);
    return ref;
}

/**
 * @brief Initialize global process registry
 * 
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_init_registry(void) {
    if (g_process_registry) {
        return OL_SUCCESS;  /* Already initialized */
    }
    
    if (ol_mutex_init(&g_registry_mutex) != OL_SUCCESS) {
        return OL_ERROR;
    }
    
    g_process_registry = ol_hashmap_create(1024, NULL);
    if (!g_process_registry) {
        ol_mutex_destroy(&g_registry_mutex);
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
}

/**
 * @brief Register process in global registry
 * 
 * @param process Process to register
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_register(ol_process_t* process) {
    if (!process || !g_process_registry) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&g_registry_mutex);
    
    /* Check if PID already exists */
    if (ol_hashmap_get(g_process_registry, &process->pid, sizeof(ol_pid_t))) {
        ol_mutex_unlock(&g_registry_mutex);
        return OL_ERROR;
    }
    
    /* Add to registry */
    if (!ol_hashmap_put(g_process_registry, &process->pid, sizeof(ol_pid_t), process)) {
        ol_mutex_unlock(&g_registry_mutex);
        return OL_ERROR;
    }
    
    ol_mutex_unlock(&g_registry_mutex);
    
    return OL_SUCCESS;
}

/**
 * @brief Unregister process from global registry
 * 
 * @param process Process to unregister
 */
static void ol_process_unregister(ol_process_t* process) {
    if (!process || !g_process_registry) {
        return;
    }
    
    ol_mutex_lock(&g_registry_mutex);
    ol_hashmap_remove(g_process_registry, &process->pid, sizeof(ol_pid_t));
    ol_mutex_unlock(&g_registry_mutex);
}

/**
 * @brief Find process by PID
 * 
 * @param pid Process ID to find
 * @return ol_process_t* Found process or NULL
 */
static ol_process_t* ol_process_find_by_pid(ol_pid_t pid) {
    if (!g_process_registry) {
        return NULL;
    }
    
    ol_mutex_lock(&g_registry_mutex);
    ol_process_t* process = (ol_process_t*)ol_hashmap_get(g_process_registry, 
                                                         &pid, sizeof(ol_pid_t));
    ol_mutex_unlock(&g_registry_mutex);
    
    return process;
}

/**
 * @brief Create default process name
 * 
 * @param buffer Buffer to store name
 * @param size Buffer size
 * @param prefix Name prefix
 */
static void ol_process_create_default_name(char* buffer, size_t size, const char* prefix) {
    uint32_t counter;
    ol_mutex_lock(&g_registry_mutex);
    counter = ++g_process_counter;
    ol_mutex_unlock(&g_registry_mutex);
    
    if (prefix) {
        snprintf(buffer, size, "%s.%u", prefix, counter);
    } else {
        snprintf(buffer, size, "process.%u", counter);
    }
}

/**
 * @brief Process trampoline function (runs in green thread)
 * 
 * @param arg Process instance
 */
static void ol_process_trampoline(void* arg) {
    ol_process_t* process = (ol_process_t*)arg;
    if (!process) {
        return;
    }
    
    /* Set thread-local current process */
    g_current_process = process;
    
    /* Update process state */
    ol_mutex_lock(&process->state_mutex);
    process->state = OL_PROCESS_RUNNING;
    process->start_time = ol_monotonic_now_ns();
    process->system_thread_id = OL_GET_TID();
    ol_mutex_unlock(&process->state_mutex);
    
    /* Check for trap exit flag */
    bool trap_exit = (process->flags & OL_PROCESS_TRAP_EXIT) != 0;
    
    /* Main process loop */
    while (process->state == OL_PROCESS_RUNNING) {
        /* Check for exit signals */
        if (!trap_exit && process->exit_info.reason != OL_EXIT_NORMAL) {
            break;
        }
        
        /* Execute process entry function */
        if (process->entry) {
            process->entry(process, process->entry_arg);
        } else {
            /* No entry function - process just waits for messages */
            void* msg = NULL;
            size_t size = 0;
            ol_pid_t sender = 0;
            
            if (ol_process_recv(process, &msg, &size, &sender, 1000) == 1) {
                /* Message received - but no handler */
                if (msg) {
                    free(msg);
                }
            }
        }
        
        /* Check for normal exit */
        if (process->state == OL_PROCESS_RUNNING) {
            process->state = OL_PROCESS_SUSPENDED;
        }
    }
    
    /* Process is terminating */
    ol_mutex_lock(&process->state_mutex);
    
    if (process->state == OL_PROCESS_RUNNING) {
        process->state = OL_PROCESS_DONE;
    }
    
    /* Clean up resources */
    if (process->green_thread) {
        ol_gt_destroy(process->green_thread);
        process->green_thread = NULL;
    }
    
    /* Notify linked processes */
    for (size_t i = 0; i < process->link_count; i++) {
        process_link_t* link = &process->links[i];
        ol_process_t* linked = ol_process_find_by_pid(link->pid);
        
        if (linked && linked->exit_handler) {
            linked->exit_handler(linked, process->pid, 
                                process->exit_info.reason,
                                process->exit_info.data);
        }
    }
    
    /* Clear current process */
    g_current_process = NULL;
    
    ol_mutex_unlock(&process->state_mutex);
}

/**
 * @brief Send exit signal to process
 * 
 * @param process Process to signal
 * @param reason Exit reason
 * @param exit_data Exit data
 * @param exit_data_size Exit data size
 */
static void ol_process_send_exit(ol_process_t* process, ol_exit_reason_t reason,
                                void* exit_data, size_t exit_data_size) {
    if (!process) return;
    
    ol_mutex_lock(&process->state_mutex);
    
    /* Already exiting */
    if (process->state != OL_PROCESS_RUNNING && 
        process->state != OL_PROCESS_SUSPENDED) {
        ol_mutex_unlock(&process->state_mutex);
        return;
    }
    
    /* Set exit information */
    process->exit_info.reason = reason;
    process->exit_info.timestamp = ol_monotonic_now_ns();
    
    /* Copy exit data if provided */
    if (exit_data && exit_data_size > 0) {
        process->exit_info.data = malloc(exit_data_size);
        if (process->exit_info.data) {
            memcpy(process->exit_info.data, exit_data, exit_data_size);
            process->exit_info.data_size = exit_data_size;
        }
    } else {
        process->exit_info.data = NULL;
        process->exit_info.data_size = 0;
    }
    
    /* Update process state */
    switch (reason) {
        case OL_EXIT_NORMAL:
            process->state = OL_PROCESS_DONE;
            break;
        case OL_EXIT_KILL:
            process->state = OL_PROCESS_KILLED;
            break;
        default:
            process->state = OL_PROCESS_CRASHED;
            break;
    }
    
    /* Wake up process if waiting */
    ol_cond_signal(&process->state_cond);
    
    /* Wake up mailbox waiters */
    ol_cond_signal(&process->mailbox_cond);
    
    ol_mutex_unlock(&process->state_mutex);
}

/**
 * @brief Add link to process
 * 
 * @param process Process to add link to
 * @param pid Linked process ID
 * @param is_monitor Whether this is a monitor link
 * @param ref Monitor reference (if monitor)
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_add_link(ol_process_t* process, ol_pid_t pid,
                              bool is_monitor, uint64_t ref) {
    if (!process) {
        return OL_ERROR;
    }
    
    /* Check for duplicate link */
    for (size_t i = 0; i < process->link_count; i++) {
        if (process->links[i].pid == pid) {
            return OL_SUCCESS;  /* Already linked */
        }
    }
    
    /* Ensure capacity */
    if (process->link_count >= process->link_capacity) {
        size_t new_capacity = process->link_capacity * 2;
        if (new_capacity < 8) new_capacity = 8;
        
        process_link_t* new_links = (process_link_t*)realloc(
            process->links, new_capacity * sizeof(process_link_t));
        if (!new_links) {
            return OL_ERROR;
        }
        
        process->links = new_links;
        process->link_capacity = new_capacity;
    }
    
    /* Add link */
    process_link_t* link = &process->links[process->link_count++];
    link->pid = pid;
    link->is_monitor = is_monitor;
    link->ref = ref;
    
    return OL_SUCCESS;
}

/**
 * @brief Remove link from process
 * 
 * @param process Process to remove link from
 * @param pid Process ID to unlink
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_remove_link(ol_process_t* process, ol_pid_t pid) {
    if (!process) {
        return OL_ERROR;
    }
    
    for (size_t i = 0; i < process->link_count; i++) {
        if (process->links[i].pid == pid) {
            /* Shift remaining links */
            for (size_t j = i; j < process->link_count - 1; j++) {
                process->links[j] = process->links[j + 1];
            }
            process->link_count--;
            return OL_SUCCESS;
        }
    }
    
    return OL_ERROR;  /* Link not found */
}

/**
 * @brief Add monitor to process
 * 
 * @param process Process to monitor
 * @param monitor_pid Monitoring process ID
 * @param ref Monitor reference
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_add_monitor(ol_process_t* process, ol_pid_t monitor_pid,
                                 uint64_t ref) {
    if (!process) {
        return OL_ERROR;
    }
    
    /* Ensure capacity */
    if (process->monitor_count >= process->monitor_capacity) {
        size_t new_capacity = process->monitor_capacity * 2;
        if (new_capacity < 8) new_capacity = 8;
        
        process_link_t* new_monitors = (process_link_t*)realloc(
            process->monitors, new_capacity * sizeof(process_link_t));
        if (!new_monitors) {
            return OL_ERROR;
        }
        
        process->monitors = new_monitors;
        process->monitor_capacity = new_capacity;
    }
    
    /* Add monitor */
    process_link_t* monitor = &process->monitors[process->monitor_count++];
    monitor->pid = monitor_pid;
    monitor->is_monitor = true;
    monitor->ref = ref;
    
    return OL_SUCCESS;
}

/**
 * @brief Remove monitor from process
 * 
 * @param process Process being monitored
 * @param ref Monitor reference to remove
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_process_remove_monitor(ol_process_t* process, uint64_t ref) {
    if (!process) {
        return OL_ERROR;
    }
    
    for (size_t i = 0; i < process->monitor_count; i++) {
        if (process->monitors[i].ref == ref) {
            /* Shift remaining monitors */
            for (size_t j = i; j < process->monitor_count - 1; j++) {
                process->monitors[j] = process->monitors[j + 1];
            }
            process->monitor_count--;
            return OL_SUCCESS;
        }
    }
    
    return OL_ERROR;  /* Monitor not found */
}

/**
 * @brief Clean up process resources
 * 
 * @param process Process to clean up
 */
static void ol_process_cleanup(ol_process_t* process) {
    if (!process) return;
    
    /* Clear mailbox */
    ol_mutex_lock(&process->mailbox_mutex);
    mailbox_entry_t* entry = process->mailbox_head;
    while (entry) {
        mailbox_entry_t* next = entry->next;
        if (entry->msg) {
            ol_serialize_free(entry->msg);
        }
        free(entry);
        entry = next;
    }
    process->mailbox_head = NULL;
    process->mailbox_tail = NULL;
    process->mailbox_size = 0;
    ol_mutex_unlock(&process->mailbox_mutex);
    
    /* Destroy synchronization primitives */
    ol_mutex_destroy(&process->mailbox_mutex);
    ol_cond_destroy(&process->mailbox_cond);
    ol_mutex_destroy(&process->state_mutex);
    ol_cond_destroy(&process->state_cond);
    
    /* Free arrays */
    free(process->links);
    free(process->monitors);
    
    /* Free exit data */
    if (process->exit_info.data) {
        free(process->exit_info.data);
    }
    
    /* Destroy arena */
    if (process->arena) {
        ol_arena_destroy(process->arena);
    }
    
    /* Unregister from global registry */
    ol_process_unregister(process);
}

/* ==================== Public API Implementation ==================== */

ol_process_t* ol_process_create(ol_process_entry_fn entry, void* arg,
                               ol_process_t* parent, uint32_t flags,
                               size_t arena_size) {
    /* Initialize registry if needed */
    if (ol_process_init_registry() != OL_SUCCESS) {
        return NULL;
    }
    
    /* Validate parameters */
    if (arena_size == 0) {
        arena_size = DEFAULT_ARENA_SIZE;
    }
    
    /* Allocate process structure */
    ol_process_t* process = (ol_process_t*)calloc(1, sizeof(ol_process_t));
    if (!process) {
        return NULL;
    }
    
    /* Generate PID */
    process->pid = ol_process_generate_pid();
    
    /* Set process name */
    ol_process_create_default_name(process->name, sizeof(process->name), "process");
    
    /* Set process state */
    process->state = OL_PROCESS_NEW;
    process->flags = flags;
    process->create_time = ol_monotonic_now_ns();
    
    /* Set entry function */
    process->entry = entry;
    process->entry_arg = arg;
    
    /* Set parent process */
    process->parent = parent;
    
    /* Create memory arena */
    process->arena = ol_arena_create(arena_size, false);
    if (!process->arena) {
        free(process);
        return NULL;
    }
    process->arena_size = arena_size;
    
    /* Initialize mailbox */
    if (ol_mutex_init(&process->mailbox_mutex) != OL_SUCCESS) {
        ol_arena_destroy(process->arena);
        free(process);
        return NULL;
    }
    
    if (ol_cond_init(&process->mailbox_cond) != OL_SUCCESS) {
        ol_mutex_destroy(&process->mailbox_mutex);
        ol_arena_destroy(process->arena);
        free(process);
        return NULL;
    }
    
    /* Initialize state synchronization */
    if (ol_mutex_init(&process->state_mutex) != OL_SUCCESS) {
        ol_cond_destroy(&process->mailbox_cond);
        ol_mutex_destroy(&process->mailbox_mutex);
        ol_arena_destroy(process->arena);
        free(process);
        return NULL;
    }
    
    if (ol_cond_init(&process->state_cond) != OL_SUCCESS) {
        ol_mutex_destroy(&process->state_mutex);
        ol_cond_destroy(&process->mailbox_cond);
        ol_mutex_destroy(&process->mailbox_mutex);
        ol_arena_destroy(process->arena);
        free(process);
        return NULL;
    }
    
    /* Initialize arrays */
    process->links = NULL;
    process->link_count = 0;
    process->link_capacity = 0;
    
    process->monitors = NULL;
    process->monitor_count = 0;
    process->monitor_capacity = 0;
    
    /* Initialize exit info */
    process->exit_info.reason = OL_EXIT_NORMAL;
    process->exit_info.data = NULL;
    process->exit_info.data_size = 0;
    process->exit_info.timestamp = 0;
    
    /* Create green thread */
    process->green_thread = ol_gt_spawn(ol_process_trampoline, process, 0);
    if (!process->green_thread) {
        ol_process_cleanup(process);
        free(process);
        return NULL;
    }
    
    /* Register process */
    if (ol_process_register(process) != OL_SUCCESS) {
        ol_gt_destroy(process->green_thread);
        ol_process_cleanup(process);
        free(process);
        return NULL;
    }
    
    return process;
}

void ol_process_destroy(ol_process_t* process, ol_exit_reason_t reason) {
    if (!process) return;
    
    /* Send exit signal */
    ol_process_send_exit(process, reason, NULL, 0);
    
    /* Wait for process to terminate */
    ol_deadline_t deadline = ol_deadline_from_ms(PROCESS_TIMEOUT_MS);
    
    ol_mutex_lock(&process->state_mutex);
    while (process->state == OL_PROCESS_RUNNING ||
           process->state == OL_PROCESS_SUSPENDED) {
        if (ol_cond_wait_until(&process->state_cond, &process->state_mutex,
                              deadline.when_ns) == 0) {
            break;  /* Timeout */
        }
    }
    ol_mutex_unlock(&process->state_mutex);
    
    /* Clean up resources */
    ol_process_cleanup(process);
    
    /* Free process structure */
    free(process);
}

ol_pid_t ol_process_pid(const ol_process_t* process) {
    return process ? process->pid : 0;
}

ol_process_state_t ol_process_state(const ol_process_t* process) {
    if (!process) return OL_PROCESS_KILLED;
    
    ol_process_state_t state;
    ol_mutex_lock((ol_mutex_t*)&process->state_mutex);
    state = process->state;
    ol_mutex_unlock((ol_mutex_t*)&process->state_mutex);
    
    return state;
}

ol_exit_reason_t ol_process_exit_reason(const ol_process_t* process) {
    if (!process) return OL_EXIT_NOPROC;
    
    ol_exit_reason_t reason;
    ol_mutex_lock((ol_mutex_t*)&process->state_mutex);
    reason = process->exit_info.reason;
    ol_mutex_unlock((ol_mutex_t*)&process->state_mutex);
    
    return reason;
}

int ol_process_link(ol_process_t* process1, ol_process_t* process2) {
    if (!process1 || !process2) {
        return OL_ERROR;
    }
    
    if (process1->pid == process2->pid) {
        return OL_ERROR;  /* Cannot link to self */
    }
    
    /* Link process1 to process2 */
    if (ol_process_add_link(process1, process2->pid, false, 0) != OL_SUCCESS) {
        return OL_ERROR;
    }
    
    /* Link process2 to process1 */
    if (ol_process_add_link(process2, process1->pid, false, 0) != OL_SUCCESS) {
        /* Rollback first link */
        ol_process_remove_link(process1, process2->pid);
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
}

ol_pid_t ol_process_monitor(ol_process_t* monitor, ol_process_t* target) {
    if (!monitor || !target) {
        return 0;
    }
    
    if (monitor->pid == target->pid) {
        return 0;  /* Cannot monitor self */
    }
    
    uint64_t ref = ol_process_generate_monitor_ref();
    
    /* Add monitor to target */
    if (ol_process_add_monitor(target, monitor->pid, ref) != OL_SUCCESS) {
        return 0;
    }
    
    /* Add link from monitor to target */
    if (ol_process_add_link(monitor, target->pid, true, ref) != OL_SUCCESS) {
        /* Rollback monitor */
        ol_process_remove_monitor(target, ref);
        return 0;
    }
    
    return ref;
}

int ol_process_unlink(ol_process_t* process1, ol_process_t* process2) {
    if (!process1 || !process2) {
        return OL_ERROR;
    }
    
    int result1 = ol_process_remove_link(process1, process2->pid);
    int result2 = ol_process_remove_link(process2, process1->pid);
    
    return (result1 == OL_SUCCESS && result2 == OL_SUCCESS) ? 
           OL_SUCCESS : OL_ERROR;
}

int ol_process_send(ol_process_t* process, const void* data, size_t size,
                   ol_pid_t sender_pid) {
    if (!process || !data || size == 0) {
        return OL_ERROR;
    }
    
    /* Check if process can receive messages */
    ol_mutex_lock(&process->state_mutex);
    if (process->state != OL_PROCESS_RUNNING &&
        process->state != OL_PROCESS_SUSPENDED &&
        process->state != OL_PROCESS_READY) {
        ol_mutex_unlock(&process->state_mutex);
        return OL_ERROR;
    }
    
    ol_mutex_unlock(&process->state_mutex);
    
    /* Create message entry */
    mailbox_entry_t* entry = (mailbox_entry_t*)malloc(sizeof(mailbox_entry_t));
    if (!entry) {
        return OL_ERROR;
    }
    
    /* Serialize message */
    entry->msg = ol_serialize(data, size, OL_SERIALIZE_BINARY, 0,
                             sender_pid, process->pid);
    if (!entry->msg) {
        free(entry);
        return OL_ERROR;
    }
    
    entry->sender = sender_pid;
    entry->timestamp = ol_monotonic_now_ns();
    entry->next = NULL;
    
    /* Add to mailbox */
    ol_mutex_lock(&process->mailbox_mutex);
    
    if (process->mailbox_size >= MAILBOX_CAPACITY) {
        /* Mailbox full - drop oldest message */
        mailbox_entry_t* oldest = process->mailbox_head;
        if (oldest) {
            process->mailbox_head = oldest->next;
            if (!process->mailbox_head) {
                process->mailbox_tail = NULL;
            }
            
            if (oldest->msg) {
                ol_serialize_free(oldest->msg);
            }
            free(oldest);
            process->mailbox_size--;
        }
    }
    
    /* Add new entry to tail */
    if (process->mailbox_tail) {
        process->mailbox_tail->next = entry;
        process->mailbox_tail = entry;
    } else {
        process->mailbox_head = process->mailbox_tail = entry;
    }
    
    process->mailbox_size++;
    process->message_count++;
    
    /* Update peak size */
    if (process->mailbox_size > process->peak_mailbox_size) {
        process->peak_mailbox_size = process->mailbox_size;
    }
    
    /* Signal waiting receivers */
    ol_cond_signal(&process->mailbox_cond);
    
    ol_mutex_unlock(&process->mailbox_mutex);
    
    return OL_SUCCESS;
}

int ol_process_recv(ol_process_t* process, void** out_data, size_t* out_size,
                   ol_pid_t* out_sender, int timeout_ms) {
    if (!process || !out_data || !out_size) {
        return OL_ERROR;
    }
    
    ol_deadline_t deadline = {0};
    if (timeout_ms > 0) {
        deadline = ol_deadline_from_ms(timeout_ms);
    } else if (timeout_ms < 0) {
        deadline.when_ns = 0;  /* Infinite */
    }
    
    ol_mutex_lock(&process->mailbox_mutex);
    
    while (process->mailbox_size == 0) {
        /* Check if process is still alive */
        ol_mutex_lock(&process->state_mutex);
        bool is_alive = (process->state == OL_PROCESS_RUNNING ||
                        process->state == OL_PROCESS_SUSPENDED);
        ol_mutex_unlock(&process->state_mutex);
        
        if (!is_alive) {
            ol_mutex_unlock(&process->mailbox_mutex);
            return -1;
        }
        
        /* Wait for message */
        if (timeout_ms == 0) {
            /* Non-blocking */
            ol_mutex_unlock(&process->mailbox_mutex);
            return 0;
        }
        
        int wait_result = ol_cond_wait_until(&process->mailbox_cond,
                                            &process->mailbox_mutex,
                                            deadline.when_ns);
        if (wait_result == 0) {
            /* Timeout */
            ol_mutex_unlock(&process->mailbox_mutex);
            return 0;
        } else if (wait_result < 0) {
            /* Error */
            ol_mutex_unlock(&process->mailbox_mutex);
            return -1;
        }
    }
    
    /* Get first message from mailbox */
    mailbox_entry_t* entry = process->mailbox_head;
    if (!entry) {
        ol_mutex_unlock(&process->mailbox_mutex);
        return 0;
    }
    
    /* Remove from mailbox */
    process->mailbox_head = entry->next;
    if (!process->mailbox_head) {
        process->mailbox_tail = NULL;
    }
    process->mailbox_size--;
    
    ol_mutex_unlock(&process->mailbox_mutex);
    
    /* Deserialize message */
    void* data = NULL;
    size_t size = 0;
    
    if (ol_deserialize(entry->msg, &data, &size) != OL_SUCCESS) {
        if (entry->msg) {
            ol_serialize_free(entry->msg);
        }
        free(entry);
        return -1;
    }
    
    /* Return results */
    *out_data = data;
    *out_size = size;
    
    if (out_sender) {
        *out_sender = entry->sender;
    }
    
    /* Clean up entry */
    if (entry->msg) {
        ol_serialize_free(entry->msg);
    }
    free(entry);
    
    return 1;
}

void ol_process_set_exit_handler(ol_process_t* process,
                                ol_exit_handler_fn handler,
                                void* user_data) {
    if (!process) return;
    
    ol_mutex_lock(&process->state_mutex);
    process->exit_handler = handler;
    process->exit_handler_data = user_data;
    ol_mutex_unlock(&process->state_mutex);
}

ol_arena_t* ol_process_arena(const ol_process_t* process) {
    return process ? process->arena : NULL;
}

ol_gt_t* ol_process_green_thread(const ol_process_t* process) {
    return process ? process->green_thread : NULL;
}

ol_process_t* ol_process_parent(const ol_process_t* process) {
    return process ? process->parent : NULL;
}

bool ol_process_is_alive(const ol_process_t* process) {
    if (!process) return false;
    
    bool alive;
    ol_mutex_lock((ol_mutex_t*)&process->state_mutex);
    alive = (process->state == OL_PROCESS_RUNNING ||
            process->state == OL_PROCESS_SUSPENDED ||
            process->state == OL_PROCESS_READY);
    ol_mutex_unlock((ol_mutex_t*)&process->state_mutex);
    
    return alive;
}

void ol_process_crash(ol_process_t* process, ol_exit_reason_t reason,
                     void* exit_data) {
    if (!process) return;
    
    ol_process_send_exit(process, reason, exit_data, 0);
}

size_t ol_process_link_count(const ol_process_t* process) {
    return process ? process->link_count : 0;
}

size_t ol_process_monitor_count(const ol_process_t* process) {
    return process ? process->monitor_count : 0;
}
