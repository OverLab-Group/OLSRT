/**
 * @file ol_supervisor.h
 * @brief Supervisor System for OLSRT
 * 
 * @details
 * This module implements a supervision tree for managing actor lifecycles.
 * Supervisors monitor child actors and restart them based on configurable strategies.
 * 
 * Features:
 * - Multiple supervision strategies
 * - Configurable restart intensity
 * - Hierarchical supervision
 * - Graceful shutdown
 * 
 * @note
 * - Thread-safe for all public APIs
 * - Memory-safe with clear ownership
 * - Cross-platform compatible
 */

#ifndef OL_SUPERVISOR_H
#define OL_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ol_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

/**
 * @brief Supervisor instance
 */
typedef struct ol_supervisor ol_supervisor_t;

/**
 * @brief Child process function type
 * 
 * @param arg User argument
 * @return int Exit status (0 = normal, non-zero = error)
 */
typedef int (*ol_child_function)(void* arg);

/**
 * @brief Child restart policy
 */
typedef enum {
    OL_CHILD_PERMANENT = 0,    /**< Always restart on failure */
    OL_CHILD_TRANSIENT = 1,    /**< Restart only on abnormal exit */
    OL_CHILD_TEMPORARY = 2     /**< Never restart */
} ol_child_policy_t;

/**
 * @brief Supervisor strategy
 */
typedef enum {
    OL_SUP_ONE_FOR_ONE = 0,    /**< Restart only failed child */
    OL_SUP_ONE_FOR_ALL = 1,    /**< Restart all children */
    OL_SUP_REST_FOR_ONE = 2    /**< Restart failed and subsequent children */
} ol_supervisor_strategy_t;

/**
 * @brief Child specification
 */
typedef struct {
    const char* name;          /**< Child name (for logging) */
    ol_child_function fn;      /**< Child function */
    void* arg;                 /**< Function argument */
    ol_child_policy_t policy;  /**< Restart policy */
    uint32_t shutdown_timeout_ms; /**< Graceful shutdown timeout */
} ol_child_spec_t;

/**
 * @brief Child status information
 */
typedef struct {
    uint32_t id;               /**< Child ID */
    const char* name;          /**< Child name */
    bool is_running;           /**< Running status */
    int exit_status;           /**< Last exit status */
    int restart_count;         /**< Number of restarts */
    uint64_t uptime_ms;        /**< Current uptime */
} ol_child_status_t;

/**
 * @brief Supervisor configuration
 */
typedef struct {
    ol_supervisor_strategy_t strategy; /**< Supervision strategy */
    int max_restarts;                  /**< Max restarts in window */
    int restart_window_ms;             /**< Restart window in ms */
    bool enable_logging;               /**< Enable supervisor logging */
} ol_supervisor_config_t;

/* ==================== Supervisor Lifecycle ==================== */

/**
 * @brief Create a new supervisor
 * 
 * @param config Supervisor configuration
 * @return ol_supervisor_t* New supervisor, NULL on failure
 */
ol_supervisor_t* ol_supervisor_create(const ol_supervisor_config_t* config);

/**
 * @brief Start supervisor and all child processes
 * 
 * @param supervisor Supervisor instance
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_start(ol_supervisor_t* supervisor);

/**
 * @brief Stop supervisor and all child processes
 * 
 * @param supervisor Supervisor instance
 * @param graceful If true, wait for graceful shutdown
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_stop(ol_supervisor_t* supervisor, bool graceful);

/**
 * @brief Destroy supervisor and free resources
 * 
 * @param supervisor Supervisor instance (can be NULL)
 */
void ol_supervisor_destroy(ol_supervisor_t* supervisor);

/* ==================== Child Management ==================== */

/**
 * @brief Add a child to supervisor
 * 
 * @param supervisor Supervisor instance
 * @param spec Child specification
 * @return uint32_t Child ID, 0 on error
 */
uint32_t ol_supervisor_add_child(ol_supervisor_t* supervisor, const ol_child_spec_t* spec);

/**
 * @brief Remove a child from supervisor
 * 
 * @param supervisor Supervisor instance
 * @param child_id Child ID to remove
 * @param graceful If true, wait for graceful stop
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_remove_child(ol_supervisor_t* supervisor, uint32_t child_id, bool graceful);

/**
 * @brief Restart a specific child
 * 
 * @param supervisor Supervisor instance
 * @param child_id Child ID to restart
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_restart_child(ol_supervisor_t* supervisor, uint32_t child_id);

/**
 * @brief Get child status
 * 
 * @param supervisor Supervisor instance
 * @param child_id Child ID
 * @param status Output status structure
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_get_child_status(ol_supervisor_t* supervisor, uint32_t child_id, 
                                   ol_child_status_t* status);

/* ==================== Introspection ==================== */

/**
 * @brief Get number of managed children
 * 
 * @param supervisor Supervisor instance
 * @return size_t Number of children
 */
size_t ol_supervisor_child_count(const ol_supervisor_t* supervisor);

/**
 * @brief Check if supervisor is running
 * 
 * @param supervisor Supervisor instance
 * @return bool true if running
 */
bool ol_supervisor_is_running(const ol_supervisor_t* supervisor);

/**
 * @brief Get supervisor configuration
 * 
 * @param supervisor Supervisor instance
 * @param config Output configuration
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_get_config(const ol_supervisor_t* supervisor, ol_supervisor_config_t* config);

/**
 * @brief Set supervisor configuration
 * 
 * @param supervisor Supervisor instance
 * @param config New configuration
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_set_config(ol_supervisor_t* supervisor, const ol_supervisor_config_t* config);

/* ==================== Utility Functions ==================== */

/**
 * @brief Get default supervisor configuration
 * 
 * @return ol_supervisor_config_t Default configuration
 */
ol_supervisor_config_t ol_supervisor_default_config(void);

/**
 * @brief Create child specification
 * 
 * @param name Child name
 * @param fn Child function
 * @param arg Function argument
 * @param policy Restart policy
 * @param shutdown_timeout_ms Shutdown timeout
 * @return ol_child_spec_t Child specification
 */
ol_child_spec_t ol_child_spec_create(const char* name, ol_child_function fn, void* arg,
                                     ol_child_policy_t policy, uint32_t shutdown_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* OL_SUPERVISOR_H */