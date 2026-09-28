#!/usr/bin/env python3
"""
Patch includes/code/streams/ol_supervisor.h to add three missing
declarations that ol_supervisor.c already relies on:

  1. `uint32_t shutdown_timeout_ms` member in ol_supervisor_config_t
  2. `size_t arena_size` member in ol_child_spec_t
  3. `ol_supervisor_stats_t` type + prototype for ol_supervisor_get_stats

Idempotent: rerunning is safe.
"""

import argparse
import sys
from pathlib import Path

CONFIG_SEARCH = """typedef struct {
    ol_supervisor_strategy_t strategy; /**< Supervision strategy */
    int max_restarts;                  /**< Max restarts in window */
    int restart_window_ms;             /**< Restart window in ms */
    bool enable_logging;               /**< Enable supervisor logging */
} ol_supervisor_config_t;"""

CONFIG_REPLACE = """typedef struct {
    ol_supervisor_strategy_t strategy; /**< Supervision strategy */
    int max_restarts;                  /**< Max restarts in window */
    int restart_window_ms;             /**< Restart window in ms */
    bool enable_logging;               /**< Enable supervisor logging */
    uint32_t shutdown_timeout_ms;      /**< Max ms to wait for graceful child stop */
} ol_supervisor_config_t;"""

CHILD_SEARCH = """typedef struct {
    const char* name;          /**< Child name (for logging) */
    ol_child_function fn;      /**< Child function */
    void* arg;                 /**< Function argument */
    ol_child_policy_t policy;  /**< Restart policy */
    uint32_t shutdown_timeout_ms; /**< Graceful shutdown timeout */
} ol_child_spec_t;"""

CHILD_REPLACE = """typedef struct {
    const char* name;          /**< Child name (for logging) */
    ol_child_function fn;      /**< Child function */
    void* arg;                 /**< Function argument */
    ol_child_policy_t policy;  /**< Restart policy */
    uint32_t shutdown_timeout_ms; /**< Graceful shutdown timeout */
    size_t   arena_size;       /**< Per-child memory arena size in bytes (0 = default) */
} ol_child_spec_t;"""

STATS_ANCHOR_SEARCH = """/**
 * @brief Supervisor configuration
 */"""

STATS_BLOCK = """/**
 * @brief Supervisor runtime statistics
 *
 * @details Snapshot of live counters for the supervisor and its children.
 * Filled by ol_supervisor_get_stats().
 */
typedef struct {
    size_t   child_count;              /**< Current number of managed children */
    size_t   max_concurrent_children;  /**< Peak number of children seen */
    uint64_t total_restarts;           /**< Total child restarts performed */
    uint64_t total_crashes;            /**< Total child crashes observed */
    uint64_t uptime_ms;                /**< Supervisor uptime in ms */
    int      restarts_in_window;       /**< Restarts in the current intensity window */
} ol_supervisor_stats_t;

/**
 * @brief Supervisor configuration
 */"""

PROTO_SEARCH = """/**
 * @brief Set supervisor configuration
 * 
 * @param supervisor Supervisor instance
 * @param config New configuration
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_set_config(ol_supervisor_t* supervisor, const ol_supervisor_config_t* config);"""

PROTO_REPLACE = """/**
 * @brief Set supervisor configuration
 * 
 * @param supervisor Supervisor instance
 * @param config New configuration
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_set_config(ol_supervisor_t* supervisor, const ol_supervisor_config_t* config);

/**
 * @brief Get live supervisor statistics
 * 
 * @param supervisor Supervisor instance
 * @param stats Output statistics structure (must not be NULL)
 * @return int 0 on success, -1 on error
 */
int ol_supervisor_get_stats(const ol_supervisor_t* supervisor, ol_supervisor_stats_t* stats);"""

def apply_one(text, search, replace, label):
    if replace in text:
        print("[SKIP] {}: already applied".format(label))
        return text, 0
    if search not in text:
        print("[FAIL] {}: anchor not found".format(label))
        return text, 1
    return text.replace(search, replace, 1), 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=None)
    args = ap.parse_args()

    root = args.root
    if root is None:
        base = Path(__file__).resolve().parent
        for c in [base] + list(base.parents)[:5]:
            if (c / "includes" / "code" / "streams" / "ol_supervisor.h").exists():
                root = c
                break
    if root is None:
        print("[FAIL] OLSRT root not found")
        return 1

    header = root / "includes" / "code" / "streams" / "ol_supervisor.h"
    if not header.exists():
        print("[FAIL] header not found: " + str(header))
        return 1

    original = header.read_text(encoding="utf-8")
    text = original
    failures = 0

    text, f = apply_one(text, CONFIG_SEARCH, CONFIG_REPLACE,
                        "config.shutdown_timeout_ms")
    failures += f

    text, f = apply_one(text, CHILD_SEARCH, CHILD_REPLACE,
                        "child_spec.arena_size")
    failures += f

    text, f = apply_one(text, STATS_ANCHOR_SEARCH, STATS_BLOCK,
                        "ol_supervisor_stats_t typedef")
    failures += f

    text, f = apply_one(text, PROTO_SEARCH, PROTO_REPLACE,
                        "ol_supervisor_get_stats prototype")
    failures += f

    if failures:
        print("[FAIL] {} patch(es) could not be applied".format(failures))
        return 2

    if text == original:
        print("[DONE] nothing to do")
        return 0

    backup = header.with_suffix(header.suffix + ".orig")
    if not backup.exists():
        backup.write_text(original, encoding="utf-8")
        print("[OK] backup -> " + str(backup.relative_to(root)))

    header.write_text(text, encoding="utf-8")
    print("[OK] wrote -> " + str(header.relative_to(root)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
