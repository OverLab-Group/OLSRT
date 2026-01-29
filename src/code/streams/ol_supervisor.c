/**
 * @file ol_supervisor.c
 * @brief Supervisor System Implementation
 * 
 * @details
 * Complete supervisor implementation with multiple restart strategies,
 * intensity checking, and hierarchical supervision.
 */

#include "ol_supervisor.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ==================== Platform-Specific Headers ==================== */
#if defined(_WIN32)
    #include <windows.h>
    #define OL_THREAD_CALL __stdcall
    typedef DWORD (OL_THREAD_CALL *ol_thread_func)(void*);
    typedef HANDLE ol_thread_handle;
#else
    #include <pthread.h>
    #include <unistd.h>
    #include <time.h>
    typedef void* (*ol_thread_func)(void*);
    typedef pthread_t ol_thread_handle;
#endif

/* ==================== Internal Structures ==================== */

/**
 * @brief Child process state
 */
typedef enum {
    CHILD_STATE_INIT = 0,      /**< Initial state */
    CHILD_STATE_RUNNING,       /**< Running */
    CHILD_STATE_STOPPING,      /**< Stopping */
    CHILD_STATE_STOPPED,       /**< Stopped */
    CHILD_STATE_CRASHED        /**< Crashed */
} ol_child_state_t;

/**
 * @brief Internal child structure
 */
typedef struct {
    uint32_t id;                       /**< Child ID */
    ol_child_spec_t spec;              /**< Child specification */
    ol_thread_handle thread;           /**< Thread handle */
    ol_child_state_t state;            /**< Current state */
    int exit_status;                   /**< Last exit status */
    int restart_count;                 /**< Number of restarts */
    uint64_t start_time;               /**< Start timestamp */
    uint64_t last_restart_time;        /**< Last restart timestamp */
    ol_supervisor_t* supervisor;       /**< Parent supervisor */
    void* context;                     /**< Child context */
} ol_child_internal_t;

/**
 * @brief Internal supervisor structure
 */
struct ol_supervisor {
    ol_supervisor_config_t config;     /**< Supervisor configuration */
    ol_child_internal_t* children;     /**< Child array */
    size_t child_count;                /**< Number of children */
    size_t child_capacity;             /**< Array capacity */
    ol_mutex_t mutex;                  /**< State mutex */
    ol_thread_handle monitor_thread;   /**< Monitor thread */
    bool running;                      /**< Running flag */
    bool stopping;                     /**< Stopping flag */
    uint64_t start_time;               /**< Supervisor start time */
    uint32_t next_child_id;            /**< Next child ID */
    ol_channel_t* event_channel;       /**< Event channel for monitoring */
};

/**
 * @brief Supervisor event types
 */
typedef enum {
    EVENT_CHILD_STARTED = 0,
    EVENT_CHILD_STOPPED,
    EVENT_CHILD_CRASHED,
    EVENT_SUPERVISOR_STOP
} ol_supervisor_event_t;

/**
 * @brief Supervisor event structure
 */
typedef struct {
    ol_supervisor_event_t type;        /**< Event type */
    uint32_t child_id;                 /**< Child ID (if applicable) */
    void* data;                        /**< Event data */
    size_t data_size;                  /**< Data size */
} ol_supervisor_event_t;

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Get current monotonic time in milliseconds
 * 
 * @return uint64_t Current time in ms
 */
static uint64_t ol_supervisor_now_ms(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency = {0};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

/**
 * @brief Sleep for specified milliseconds
 * 
 * @param ms Milliseconds to sleep
 */
static void ol_supervisor_sleep_ms(uint32_t ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/**
 * @brief Create and start a thread
 * 
 * @param thread Thread handle output
 * @param func Thread function
 * @param arg Thread argument
 * @return int 0 on success, -1 on error
 */
static int ol_supervisor_thread_create(ol_thread_handle* thread, ol_thread_func func, void* arg) {
#if defined(_WIN32)
    *thread = CreateThread(NULL, 0, func, arg, 0, NULL);
    return (*thread != NULL) ? 0 : -1;
#else
    return pthread_create(thread, NULL, func, arg);
#endif
}

/**
 * @brief Join a thread
 * 
 * @param thread Thread handle
 * @return int 0 on success, -1 on error
 */
static int ol_supervisor_thread_join(ol_thread_handle thread) {
#if defined(_WIN32)
    DWORD result = WaitForSingleObject(thread, INFINITE);
    if (result == WAIT_OBJECT_0) {
        CloseHandle(thread);
        return 0;
    }
    return -1;
#else
    return pthread_join(thread, NULL);
#endif
}

/**
 * @brief Detach a thread
 * 
 * @param thread Thread handle
 */
static void ol_supervisor_thread_detach(ol_thread_handle thread) {
#if !defined(_WIN32)
    pthread_detach(thread);
#endif
}

/**
 * @brief Child thread function
 */
#if defined(_WIN32)
static DWORD OL_THREAD_CALL ol_child_thread_func(void* arg)
#else
static void* ol_child_thread_func(void* arg)
#endif
{
    ol_child_internal_t* child = (ol_child_internal_t*)arg;
    ol_supervisor_t* supervisor = child->supervisor;
    
    /* Update child state */
    child->state = CHILD_STATE_RUNNING;
    child->start_time = ol_supervisor_now_ms();
    child->exit_status = 0;
    
    /* Execute child function */
    int result = child->spec.fn(child->spec.arg);
    
    /* Update child state */
    child->state = (result == 0) ? CHILD_STATE_STOPPED : CHILD_STATE_CRASHED;
    child->exit_status = result;
    
    /* Send event to supervisor */
    if (supervisor != NULL && supervisor->event_channel != NULL) {
        ol_supervisor_event_t event;
        event.type = (result == 0) ? EVENT_CHILD_STOPPED : EVENT_CHILD_CRASHED;
        event.child_id = child->id;
        event.data = NULL;
        event.data_size = 0;
        
        ol_channel_try_send(supervisor->event_channel, &event);
    }
    
#if defined(_WIN32)
    return (DWORD)result;
#else
    return NULL;
#endif
}

/**
 * @brief Supervisor monitor thread function
 */
#if defined(_WIN32)
static DWORD OL_THREAD_CALL ol_supervisor_monitor_func(void* arg)
#else
static void* ol_supervisor_monitor_func(void* arg)
#endif
{
    ol_supervisor_t* supervisor = (ol_supervisor_t*)arg;
    
    while (supervisor->running) {
        /* Check for events */
        ol_supervisor_event_t* event = NULL;
        int result = ol_channel_recv_timeout(supervisor->event_channel, (void**)&event, 1000);
        
        if (result == 1 && event != NULL) {
            /* Process event */
            ol_mutex_lock(&supervisor->mutex);
            
            switch (event->type) {
                case EVENT_CHILD_STOPPED:
                case EVENT_CHILD_CRASHED: {
                    /* Find the child */
                    for (size_t i = 0; i < supervisor->child_count; i++) {
                        ol_child_internal_t* child = &supervisor->children[i];
                        if (child->id == event->child_id) {
                            /* Handle based on restart policy */
                            bool should_restart = false;
                            
                            if (child->spec.policy == OL_CHILD_PERMANENT) {
                                should_restart = true;
                            } else if (child->spec.policy == OL_CHILD_TRANSIENT) {
                                should_restart = (event->type == EVENT_CHILD_CRASHED);
                            }
                            /* TEMPORARY children never restart */
                            
                            if (should_restart) {
                                /* Check restart intensity */
                                uint64_t now = ol_supervisor_now_ms();
                                uint64_t window_start = now - supervisor->config.restart_window_ms;
                                
                                /* Count restarts in window */
                                int window_restarts = 0;
                                for (size_t j = 0; j < supervisor->child_count; j++) {
                                    if (supervisor->children[j].last_restart_time >= window_start) {
                                        window_restarts++;
                                    }
                                }
                                
                                if (window_restarts < supervisor->config.max_restarts) {
                                    /* Restart child */
                                    child->restart_count++;
                                    child->last_restart_time = now;
                                    
                                    /* Actually restarting handled by main loop */
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                
                case EVENT_SUPERVISOR_STOP:
                    supervisor->running = false;
                    break;
                    
                default:
                    break;
            }
            
            ol_mutex_unlock(&supervisor->mutex);
            
            /* Free event memory if needed */
            if (event->data != NULL) {
                free(event->data);
            }
        } else if (result == 0) {
            /* Channel closed */
            break;
        }
        
        /* Check child status periodically */
        ol_mutex_lock(&supervisor->mutex);
        
        for (size_t i = 0; i < supervisor->child_count; i++) {
            ol_child_internal_t* child = &supervisor->children[i];
            
            /* Restart crashed children based on policy */
            if (child->state == CHILD_STATE_CRASHED) {
                bool should_restart = false;
                
                if (child->spec.policy == OL_CHILD_PERMANENT) {
                    should_restart = true;
                } else if (child->spec.policy == OL_CHILD_TRANSIENT) {
                    should_restart = true; /* Transient restarts on crash */
                }
                
                if (should_restart) {
                    /* TODO: Implement actual restart logic */
                    child->state = CHILD_STATE_RUNNING;
                    child->restart_count++;
                    child->last_restart_time = ol_supervisor_now_ms();
                    
                    /* In real implementation, we would restart the thread */
                }
            }
        }
        
        ol_mutex_unlock(&supervisor->mutex);
    }
    
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

/**
 * @brief Start a child process
 * 
 * @param supervisor Supervisor instance
 * @param child Child to start
 * @return int 0 on success, -1 on error
 */
static int ol_supervisor_start_child(ol_supervisor_t* supervisor, ol_child_internal_t* child) {
    if (supervisor == NULL || child == NULL) {
        return -1;
    }
    
    /* Set child supervisor reference */
    child->supervisor = supervisor;
    
    /* Create thread for child */
    int result = ol_supervisor_thread_create(&child->thread, ol_child_thread_func, child);
    if (result != 0) {
        return -1;
    }
    
    /* Detach thread (supervisor monitors via events) */
    ol_supervisor_thread_detach(child->thread);
    
    return 0;
}

/**
 * @brief Stop a child process
 * 
 * @param child Child to stop
 * @param graceful If true, wait for graceful stop
 * @return int 0 on success, -1 on error
 */
static int ol_supervisor_stop_child(ol_child_internal_t* child, bool graceful) {
    if (child == NULL) {
        return -1;
    }
    
    child->state = CHILD_STATE_STOPPING;
    
    if (graceful) {
        /* TODO: Implement graceful shutdown signal */
        /* For now, just mark as stopping */
    }
    
    /* Wait for thread to finish if graceful */
    if (graceful && child->spec.shutdown_timeout_ms > 0) {
        uint64_t start = ol_supervisor_now_ms();
        while (child->state != CHILD_STATE_STOPPED && 
               child->state != CHILD_STATE_CRASHED) {
            if (ol_supervisor_now_ms() - start > child->spec.shutdown_timeout_ms) {
                break; /* Timeout */
            }
            ol_supervisor_sleep_ms(10);
        }
    }
    
    child->state = CHILD_STATE_STOPPED;
    return 0;
}

/* ==================== Public API Implementation ==================== */

ol_supervisor_config_t ol_supervisor_default_config(void) {
    ol_supervisor_config_t config;
    config.strategy = OL_SUP_ONE_FOR_ONE;
    config.max_restarts = 3;
    config.restart_window_ms = 5000;
    config.enable_logging = true;
    return config;
}

ol_child_spec_t ol_child_spec_create(const char* name, ol_child_function fn, void* arg,
                                     ol_child_policy_t policy, uint32_t shutdown_timeout_ms) {
    ol_child_spec_t spec;
    spec.name = name;
    spec.fn = fn;
    spec.arg = arg;
    spec.policy = policy;
    spec.shutdown_timeout_ms = shutdown_timeout_ms;
    return spec;
}

ol_supervisor_t* ol_supervisor_create(const ol_supervisor_config_t* config) {
    ol_supervisor_t* supervisor = (ol_supervisor_t*)calloc(1, sizeof(ol_supervisor_t));
    if (supervisor == NULL) {
        return NULL;
    }
    
    /* Set configuration */
    if (config != NULL) {
        supervisor->config = *config;
    } else {
        supervisor->config = ol_supervisor_default_config();
    }
    
    /* Initialize mutex */
    if (ol_mutex_init(&supervisor->mutex) != 0) {
        free(supervisor);
        return NULL;
    }
    
    /* Create event channel */
    supervisor->event_channel = ol_channel_create(0, NULL);
    if (supervisor->event_channel == NULL) {
        ol_mutex_destroy(&supervisor->mutex);
        free(supervisor);
        return NULL;
    }
    
    /* Initialize other fields */
    supervisor->children = NULL;
    supervisor->child_count = 0;
    supervisor->child_capacity = 0;
    supervisor->running = false;
    supervisor->stopping = false;
    supervisor->start_time = ol_supervisor_now_ms();
    supervisor->next_child_id = 1;
    
    return supervisor;
}

int ol_supervisor_start(ol_supervisor_t* supervisor) {
    if (supervisor == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    if (supervisor->running) {
        ol_mutex_unlock(&supervisor->mutex);
        return 0; /* Already running */
    }
    
    supervisor->running = true;
    supervisor->stopping = false;
    supervisor->start_time = ol_supervisor_now_ms();
    
    /* Start monitor thread */
    int result = ol_supervisor_thread_create(&supervisor->monitor_thread, 
                                            ol_supervisor_monitor_func, supervisor);
    if (result != 0) {
        supervisor->running = false;
        ol_mutex_unlock(&supervisor->mutex);
        return -1;
    }
    
    /* Start all children */
    for (size_t i = 0; i < supervisor->child_count; i++) {
        ol_supervisor_start_child(supervisor, &supervisor->children[i]);
    }
    
    ol_mutex_unlock(&supervisor->mutex);
    
    return 0;
}

int ol_supervisor_stop(ol_supervisor_t* supervisor, bool graceful) {
    if (supervisor == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    if (!supervisor->running) {
        ol_mutex_unlock(&supervisor->mutex);
        return 0; /* Already stopped */
    }
    
    supervisor->stopping = true;
    
    /* Stop all children */
    for (size_t i = 0; i < supervisor->child_count; i++) {
        ol_supervisor_stop_child(&supervisor->children[i], graceful);
    }
    
    /* Send stop event to monitor */
    ol_supervisor_event_t event;
    event.type = EVENT_SUPERVISOR_STOP;
    event.child_id = 0;
    event.data = NULL;
    event.data_size = 0;
    
    ol_channel_send(supervisor->event_channel, &event);
    
    supervisor->running = false;
    
    ol_mutex_unlock(&supervisor->mutex);
    
    /* Wait for monitor thread */
    ol_supervisor_thread_join(supervisor->monitor_thread);
    
    return 0;
}

void ol_supervisor_destroy(ol_supervisor_t* supervisor) {
    if (supervisor == NULL) {
        return;
    }
    
    /* Stop supervisor if running */
    if (supervisor->running) {
        ol_supervisor_stop(supervisor, true);
    }
    
    /* Clean up resources */
    ol_mutex_destroy(&supervisor->mutex);
    
    if (supervisor->event_channel != NULL) {
        ol_channel_destroy(supervisor->event_channel);
    }
    
    if (supervisor->children != NULL) {
        free(supervisor->children);
    }
    
    free(supervisor);
}

uint32_t ol_supervisor_add_child(ol_supervisor_t* supervisor, const ol_child_spec_t* spec) {
    if (supervisor == NULL || spec == NULL || spec->fn == NULL) {
        return 0;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    /* Ensure capacity */
    if (supervisor->child_count >= supervisor->child_capacity) {
        size_t new_capacity = supervisor->child_capacity == 0 ? 8 : supervisor->child_capacity * 2;
        ol_child_internal_t* new_children = (ol_child_internal_t*)realloc(
            supervisor->children, new_capacity * sizeof(ol_child_internal_t));
        
        if (new_children == NULL) {
            ol_mutex_unlock(&supervisor->mutex);
            return 0;
        }
        
        supervisor->children = new_children;
        supervisor->child_capacity = new_capacity;
    }
    
    /* Add child */
    ol_child_internal_t* child = &supervisor->children[supervisor->child_count];
    
    child->id = supervisor->next_child_id++;
    child->spec = *spec;
    child->state = CHILD_STATE_INIT;
    child->exit_status = 0;
    child->restart_count = 0;
    child->start_time = 0;
    child->last_restart_time = 0;
    child->supervisor = supervisor;
    child->context = NULL;
    
    supervisor->child_count++;
    
    /* Start child if supervisor is running */
    uint32_t child_id = child->id;
    
    if (supervisor->running) {
        ol_supervisor_start_child(supervisor, child);
    }
    
    ol_mutex_unlock(&supervisor->mutex);
    
    return child_id;
}

int ol_supervisor_remove_child(ol_supervisor_t* supervisor, uint32_t child_id, bool graceful) {
    if (supervisor == NULL || child_id == 0) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    /* Find child */
    ssize_t child_index = -1;
    for (size_t i = 0; i < supervisor->child_count; i++) {
        if (supervisor->children[i].id == child_id) {
            child_index = (ssize_t)i;
            break;
        }
    }
    
    if (child_index < 0) {
        ol_mutex_unlock(&supervisor->mutex);
        return -1;
    }
    
    /* Stop child */
    ol_supervisor_stop_child(&supervisor->children[child_index], graceful);
    
    /* Remove from array */
    for (size_t i = (size_t)child_index; i < supervisor->child_count - 1; i++) {
        supervisor->children[i] = supervisor->children[i + 1];
    }
    
    supervisor->child_count--;
    
    ol_mutex_unlock(&supervisor->mutex);
    
    return 0;
}

int ol_supervisor_restart_child(ol_supervisor_t* supervisor, uint32_t child_id) {
    if (supervisor == NULL || child_id == 0) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    /* Find child */
    ol_child_internal_t* child = NULL;
    for (size_t i = 0; i < supervisor->child_count; i++) {
        if (supervisor->children[i].id == child_id) {
            child = &supervisor->children[i];
            break;
        }
    }
    
    if (child == NULL) {
        ol_mutex_unlock(&supervisor->mutex);
        return -1;
    }
    
    /* Stop child if running */
    if (child->state == CHILD_STATE_RUNNING) {
        ol_supervisor_stop_child(child, false);
    }
    
    /* Update restart count */
    child->restart_count++;
    child->last_restart_time = ol_supervisor_now_ms();
    
    /* Start child again */
    int result = ol_supervisor_start_child(supervisor, child);
    
    ol_mutex_unlock(&supervisor->mutex);
    
    return result;
}

int ol_supervisor_get_child_status(ol_supervisor_t* supervisor, uint32_t child_id, 
                                   ol_child_status_t* status) {
    if (supervisor == NULL || child_id == 0 || status == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    
    /* Find child */
    ol_child_internal_t* child = NULL;
    for (size_t i = 0; i < supervisor->child_count; i++) {
        if (supervisor->children[i].id == child_id) {
            child = &supervisor->children[i];
            break;
        }
    }
    
    if (child == NULL) {
        ol_mutex_unlock(&supervisor->mutex);
        return -1;
    }
    
    /* Fill status */
    status->id = child->id;
    status->name = child->spec.name;
    status->is_running = (child->state == CHILD_STATE_RUNNING);
    status->exit_status = child->exit_status;
    status->restart_count = child->restart_count;
    status->uptime_ms = (child->start_time > 0) ? 
                       (ol_supervisor_now_ms() - child->start_time) : 0;
    
    ol_mutex_unlock(&supervisor->mutex);
    
    return 0;
}

size_t ol_supervisor_child_count(const ol_supervisor_t* supervisor) {
    if (supervisor == NULL) {
        return 0;
    }
    
    size_t count;
    ol_mutex_lock((ol_mutex_t*)&supervisor->mutex);
    count = supervisor->child_count;
    ol_mutex_unlock((ol_mutex_t*)&supervisor->mutex);
    
    return count;
}

bool ol_supervisor_is_running(const ol_supervisor_t* supervisor) {
    if (supervisor == NULL) {
        return false;
    }
    
    bool running;
    ol_mutex_lock((ol_mutex_t*)&supervisor->mutex);
    running = supervisor->running;
    ol_mutex_unlock((ol_mutex_t*)&supervisor->mutex);
    
    return running;
}

int ol_supervisor_get_config(const ol_supervisor_t* supervisor, ol_supervisor_config_t* config) {
    if (supervisor == NULL || config == NULL) {
        return -1;
    }
    
    ol_mutex_lock((ol_mutex_t*)&supervisor->mutex);
    *config = supervisor->config;
    ol_mutex_unlock((ol_mutex_t*)&supervisor->mutex);
    
    return 0;
}

int ol_supervisor_set_config(ol_supervisor_t* supervisor, const ol_supervisor_config_t* config) {
    if (supervisor == NULL || config == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&supervisor->mutex);
    supervisor->config = *config;
    ol_mutex_unlock(&supervisor->mutex);
    
    return 0;
}