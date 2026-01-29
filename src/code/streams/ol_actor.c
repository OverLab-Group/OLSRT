/**
 * @file ol_actor.c
 * @brief Actor Model Implementation
 * 
 * @details
 * Complete actor system implementation with message passing,
 * behavior management, and supervision integration.
 */

#include "ol_actor.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ==================== Platform-Specific Headers ==================== */
#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <time.h>
#endif

/* ==================== Internal Structures ==================== */

/**
 * @brief Internal actor structure
 */
struct ol_actor {
    ol_parallel_pool_t* pool;           /**< Execution pool */
    ol_channel_t* mailbox;              /**< Message channel */
    ol_actor_msg_destructor msg_dtor;   /**< Message destructor */
    ol_actor_behavior behavior;         /**< Current behavior */
    void* user_ctx;                     /**< User context */
    ol_mutex_t mutex;                   /**< State mutex */
    bool running;                       /**< Running flag */
    bool closing;                       /**< Closing flag */
    uint64_t actor_id;                  /**< Unique actor ID */
    ol_supervisor_t* supervisor;        /**< Parent supervisor (optional) */
};

/* Thread-local storage for current actor */
#if defined(_WIN32)
    static __declspec(thread) ol_actor_t* g_current_actor = NULL;
#else
    static __thread ol_actor_t* g_current_actor = NULL;
#endif

/* Global actor ID counter */
static uint64_t g_next_actor_id = 1;
static ol_mutex_t g_id_mutex = OL_MUTEX_INITIALIZER;

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Get next unique actor ID
 * 
 * @return uint64_t Unique actor ID
 */
static uint64_t ol_actor_next_id(void) {
    uint64_t id;
    ol_mutex_lock(&g_id_mutex);
    id = g_next_actor_id++;
    ol_mutex_unlock(&g_id_mutex);
    return id;
}

/**
 * @brief Get current monotonic time in milliseconds
 * 
 * @return uint64_t Current time in ms
 */
static uint64_t ol_now_ms(void) {
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
 * @brief Process a single message
 * 
 * @param actor Actor instance
 * @param message Message to process
 * @return int Processing result
 */
static int ol_actor_process_message(ol_actor_t* actor, void* message) {
    int result = 0;
    
    /* Check if this is an ask envelope */
    bool is_ask = false;
    ol_ask_envelope_t* ask_env = NULL;
    
    if (message != NULL) {
        /* Simple type checking - assume any non-NULL reply field indicates ask envelope */
        ask_env = (ol_ask_envelope_t*)message;
        if (ask_env->reply != NULL) {
            is_ask = true;
        }
    }
    
    /* Get current behavior */
    ol_actor_behavior behavior;
    ol_mutex_lock(&actor->mutex);
    behavior = actor->behavior;
    ol_mutex_unlock(&actor->mutex);
    
    /* Set thread-local current actor */
    ol_actor_t* prev_actor = g_current_actor;
    g_current_actor = actor;
    
    /* Execute behavior */
    if (behavior != NULL) {
        result = behavior(actor, message);
        
        /* Check if message was consumed */
        if (message != NULL && !is_ask) {
            /* Non-ask message not consumed - clean up */
            if (actor->msg_dtor != NULL) {
                actor->msg_dtor(message);
            }
        }
    }
    
    /* Handle unconsumed ask envelope */
    if (is_ask && ask_env != NULL && ask_env->reply != NULL) {
        /* Ask envelope not handled - cancel it */
        ol_actor_reply_cancel(ask_env);
    }
    
    /* Restore previous current actor */
    g_current_actor = prev_actor;
    
    return result;
}

/**
 * @brief Actor main loop
 * 
 * @param arg Actor instance
 */
static void ol_actor_loop(void* arg) {
    ol_actor_t* actor = (ol_actor_t*)arg;
    
    /* Main processing loop */
    while (true) {
        /* Check if we should stop */
        ol_mutex_lock(&actor->mutex);
        bool should_run = actor->running && !actor->closing;
        ol_mutex_unlock(&actor->mutex);
        
        if (!should_run) {
            break;
        }
        
        /* Receive next message */
        void* message = NULL;
        int recv_result = ol_channel_recv_timeout(actor->mailbox, &message, 1000);
        
        if (recv_result == 1) {
            /* Message received - process it */
            int behavior_result = ol_actor_process_message(actor, message);
            
            /* Check behavior result */
            if (behavior_result > 0) {
                /* Behavior requested stop */
                ol_mutex_lock(&actor->mutex);
                actor->running = false;
                ol_mutex_unlock(&actor->mutex);
            } else if (behavior_result < 0) {
                /* Behavior reported error - notify supervisor */
                if (actor->supervisor != NULL) {
                    /* TODO: Send error notification to supervisor */
                }
            }
        } else if (recv_result == 0) {
            /* Channel closed */
            ol_mutex_lock(&actor->mutex);
            actor->closing = true;
            actor->running = false;
            ol_mutex_unlock(&actor->mutex);
            break;
        } else if (recv_result == -3) {
            /* Timeout - continue */
            continue;
        } else {
            /* Error - log and continue */
            continue;
        }
    }
}

/* ==================== Public API Implementation ==================== */

ol_actor_t* ol_actor_create(ol_parallel_pool_t* pool,
                            size_t capacity,
                            ol_actor_msg_destructor dtor,
                            ol_actor_behavior initial,
                            void* user_ctx) {
    /* Validate parameters */
    if (pool == NULL || initial == NULL) {
        return NULL;
    }
    
    /* Allocate actor structure */
    ol_actor_t* actor = (ol_actor_t*)calloc(1, sizeof(ol_actor_t));
    if (actor == NULL) {
        return NULL;
    }
    
    /* Initialize fields */
    actor->pool = pool;
    actor->msg_dtor = dtor;
    actor->behavior = initial;
    actor->user_ctx = user_ctx;
    actor->running = false;
    actor->closing = false;
    actor->actor_id = ol_actor_next_id();
    actor->supervisor = NULL;
    
    /* Create mailbox */
    actor->mailbox = ol_channel_create(capacity, dtor);
    if (actor->mailbox == NULL) {
        free(actor);
        return NULL;
    }
    
    /* Initialize mutex */
    if (ol_mutex_init(&actor->mutex) != 0) {
        ol_channel_destroy(actor->mailbox);
        free(actor);
        return NULL;
    }
    
    return actor;
}

int ol_actor_start(ol_actor_t* actor) {
    if (actor == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&actor->mutex);
    
    if (actor->running) {
        ol_mutex_unlock(&actor->mutex);
        return 0; /* Already running */
    }
    
    actor->running = true;
    actor->closing = false;
    
    ol_mutex_unlock(&actor->mutex);
    
    /* Submit actor loop to pool */
    return ol_parallel_submit(actor->pool, ol_actor_loop, actor);
}

int ol_actor_stop(ol_actor_t* actor) {
    if (actor == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&actor->mutex);
    actor->running = false;
    ol_mutex_unlock(&actor->mutex);
    
    return 0;
}

int ol_actor_close(ol_actor_t* actor) {
    if (actor == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&actor->mutex);
    actor->closing = true;
    actor->running = false;
    ol_mutex_unlock(&actor->mutex);
    
    return ol_channel_close(actor->mailbox);
}

void ol_actor_destroy(ol_actor_t* actor) {
    if (actor == NULL) {
        return;
    }
    
    /* Stop actor if running */
    if (ol_actor_is_running(actor)) {
        ol_actor_close(actor);
    }
    
    /* Wait a bit for graceful shutdown */
    #if defined(_WIN32)
        Sleep(100);
    #else
        usleep(100000);
    #endif
    
    /* Clean up resources */
    ol_mutex_destroy(&actor->mutex);
    
    if (actor->mailbox != NULL) {
        ol_channel_destroy(actor->mailbox);
    }
    
    free(actor);
}

int ol_actor_send(ol_actor_t* actor, void* msg) {
    if (actor == NULL) {
        return -1;
    }
    
    return ol_channel_send(actor->mailbox, msg);
}

int ol_actor_send_timeout(ol_actor_t* actor, void* msg, uint32_t timeout_ms) {
    if (actor == NULL) {
        return -1;
    }
    
    uint64_t deadline = ol_now_ms() + timeout_ms;
    return ol_channel_send_deadline(actor->mailbox, msg, (int64_t)deadline * 1000000);
}

int ol_actor_try_send(ol_actor_t* actor, void* msg) {
    if (actor == NULL) {
        return -1;
    }
    
    return ol_channel_try_send(actor->mailbox, msg);
}

ol_future_t* ol_actor_ask(ol_actor_t* actor, void* msg) {
    if (actor == NULL) {
        return NULL;
    }
    
    /* Create promise for reply */
    ol_promise_t* promise = ol_promise_create(NULL);
    if (promise == NULL) {
        return NULL;
    }
    
    ol_future_t* future = ol_promise_get_future(promise);
    if (future == NULL) {
        ol_promise_destroy(promise);
        return NULL;
    }
    
    /* Create ask envelope */
    ol_ask_envelope_t* envelope = (ol_ask_envelope_t*)calloc(1, sizeof(ol_ask_envelope_t));
    if (envelope == NULL) {
        ol_future_destroy(future);
        ol_promise_destroy(promise);
        return NULL;
    }
    
    envelope->payload = msg;
    envelope->reply = promise;
    envelope->sender = ol_actor_self();
    envelope->ask_id = ol_now_ms(); /* Simple unique ID */
    
    /* Send envelope to actor */
    int send_result = ol_actor_send(actor, envelope);
    if (send_result != 0) {
        /* Failed to send - clean up */
        ol_actor_reply_cancel(envelope);
        ol_future_destroy(future);
        return NULL;
    }
    
    return future;
}

int ol_actor_become(ol_actor_t* actor, ol_actor_behavior behavior) {
    if (actor == NULL || behavior == NULL) {
        return -1;
    }
    
    ol_mutex_lock(&actor->mutex);
    actor->behavior = behavior;
    ol_mutex_unlock(&actor->mutex);
    
    return 0;
}

void* ol_actor_get_context(const ol_actor_t* actor) {
    if (actor == NULL) {
        return NULL;
    }
    
    ol_mutex_lock((ol_mutex_t*)&actor->mutex);
    void* context = actor->user_ctx;
    ol_mutex_unlock((ol_mutex_t*)&actor->mutex);
    
    return context;
}

void ol_actor_set_context(ol_actor_t* actor, void* context) {
    if (actor == NULL) {
        return;
    }
    
    ol_mutex_lock(&actor->mutex);
    actor->user_ctx = context;
    ol_mutex_unlock(&actor->mutex);
}

void ol_actor_reply_ok(ol_ask_envelope_t* envelope, void* value, ol_actor_value_destructor dtor) {
    if (envelope == NULL || envelope->reply == NULL) {
        return;
    }
    
    ol_promise_fulfill(envelope->reply, value, dtor);
    ol_promise_destroy(envelope->reply);
    free(envelope);
}

void ol_actor_reply_error(ol_ask_envelope_t* envelope, int error_code) {
    if (envelope == NULL || envelope->reply == NULL) {
        return;
    }
    
    ol_promise_reject(envelope->reply, error_code);
    ol_promise_destroy(envelope->reply);
    free(envelope);
}

void ol_actor_reply_cancel(ol_ask_envelope_t* envelope) {
    if (envelope == NULL || envelope->reply == NULL) {
        return;
    }
    
    ol_promise_cancel(envelope->reply);
    ol_promise_destroy(envelope->reply);
    free(envelope);
}

bool ol_actor_is_running(const ol_actor_t* actor) {
    if (actor == NULL) {
        return false;
    }
    
    bool running;
    ol_mutex_lock((ol_mutex_t*)&actor->mutex);
    running = actor->running;
    ol_mutex_unlock((ol_mutex_t*)&actor->mutex);
    
    return running;
}

size_t ol_actor_mailbox_length(const ol_actor_t* actor) {
    if (actor == NULL) {
        return 0;
    }
    
    return ol_channel_len(actor->mailbox);
}

size_t ol_actor_mailbox_capacity(const ol_actor_t* actor) {
    if (actor == NULL) {
        return 0;
    }
    
    return ol_channel_capacity(actor->mailbox);
}

ol_actor_t* ol_actor_self(void) {
    return g_current_actor;
}