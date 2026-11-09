/**
 * @file oroutines.h
 * @brief Goroutine-like cooperative oroutines (v1.3.4.1).
 *
 * @details
 * Builds on top of the v1.3.4 cooperative green-thread scheduler.
 * Each oroutine is a green thread spawned into a single-threaded
 * scheduler that multiplexes them cooperatively. Blocking channel
 * operations yield the current oroutine back to the scheduler,
 * which then runs another READY oroutine. This is the API model
 * of Go's `go` statement and channels, minus `select`.
 *
 * Semantics:
 *  - `or_go(fn, arg)` returns immediately; the scheduler will run
 *    `fn(arg)` when there are no other oroutines ahead of it.
 *  - `or_yield()` marks the current oroutine RUNNABLE and picks the
 *    next ready one. No-op if called from the main thread.
 *  - `or_join(r, deadline)` runs the scheduler until `r` is DONE
 *    or CANCELED, or the deadline expires. Returns 0 on success,
 *    -3 on timeout.
 *  - Channels have `elem_size` and `capacity`:
 *      capacity 0 = unbuffered (send blocks until a receiver)
 *      capacity N = buffered with room for N elements
 *  - `or_chan_send` on a full channel BLOCKS (yields) until there
 *    is room or the channel is closed. `or_chan_recv` on an empty
 *    channel BLOCKS until there is data or close.
 *  - `or_chan_close` unblocks all senders (with error) and all
 *    receivers (with end-of-stream). Subsequent sends return -1.
 *
 * Threading: all oroutine and channel functions must be called
 * from the same OS thread. If you need cross-thread communication,
 * use `ol_channel` or `ol_actor` instead; oroutines are
 * intentionally single-threaded cooperative, like Go's GOMAXPROCS=1.
 *
 * @author OverLab Group
 * @version 1.3.4.1
 */

#ifndef OL_OROUTINES_H
#define OL_OROUTINES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oroutine oroutine_t;
typedef struct or_chan or_chan_t;
typedef void (*or_fn)(void* arg);

/* ==================== Lifecycle ==================== */

/**
 * @brief Spawn a new oroutine.
 *
 * @param fn   Entry function. Runs cooperatively on the scheduler
 *             thread when scheduled.
 * @param arg  Opaque argument passed to @p fn.
 * @return Oroutine handle, or NULL on allocation failure.
 *
 * @note The oroutine starts in READY state. It will run when the
 *       current oroutine yields, blocks, or calls or_run/or_join.
 *       The handle remains valid until the oroutine finishes
 *       (DONE) and the caller destroys it (or until or_run returns
 *       for all oroutines).
 */
oroutine_t* or_go(or_fn fn, void* arg);

/**
 * @brief Yield the CPU to the scheduler.
 *
 * @note Marks the current oroutine RUNNABLE and switches to the
 *       next ready oroutine. If called from the main thread (no
 *       current oroutine), it returns immediately.
 */
void or_yield(void);

/**
 * @brief Wait for an oroutine to finish.
 *
 * @param r            Oroutine handle.
 * @param deadline_ns  Absolute monotonic deadline in nanoseconds
 *                     (0 = infinite).
 * @return 0 on success (DONE or CANCELED), -3 on timeout, -1 on error.
 *
 * @note This runs the scheduler until @p r reaches a terminal
 *       state. Other ready oroutines make progress meanwhile.
 *       Must be called from the main thread (not from inside an
 *       oroutine).
 */
int or_join(oroutine_t* r, int64_t deadline_ns);

/**
 * @brief Request cooperative cancellation of an oroutine.
 *
 * @param r Oroutine handle.
 *
 * @note Sets a cancellation flag. The oroutine must call
 *       or_yield() at safe points; the scheduler checks the flag
 *       on every yield and transitions the oroutine to CANCELED.
 */
void or_cancel(oroutine_t* r);

/**
 * @brief Return the currently running oroutine.
 *
 * @return Current oroutine, or NULL if called from the main thread.
 */
oroutine_t* or_self(void);

/* ==================== Scheduler ==================== */

/**
 * @brief Run the scheduler until all oroutines reach a terminal
 *        state (DONE or CANCELED).
 *
 * @return Number of oroutines completed, or -1 on error.
 *
 * @note Typical usage: spawn N oroutines with or_go(), then call
 *       or_run() once at the end of main(). Returns when all
 *       spawned oroutines are done. If a deadlock is detected
 *       (all oroutines BLOCKED and no external event can wake
 *       them), returns the count anyway — the caller can inspect
 *       per-oroutine state to detect stuck workers.
 */
int or_run(void);

/* ==================== Channels ==================== */

/**
 * @brief Create a channel with fixed-size elements.
 *
 * @param elem_size  Size in bytes of each element (1..65536).
 * @param capacity   Buffer capacity (0 = unbuffered).
 * @return Channel handle, or NULL on allocation failure or bad args.
 */
or_chan_t* or_chan_create(size_t elem_size, size_t capacity);

/**
 * @brief Send an element to a channel.
 *
 * @param ch    Channel handle.
 * @param elem  Pointer to element to copy into the channel.
 * @return 0 on success, -1 if the channel is closed or NULL.
 *
 * @note Copies @p elem_size bytes from @p elem. On a full channel,
 *       yields to the scheduler until a slot is free.
 */
int or_chan_send(or_chan_t* ch, const void* elem);

/**
 * @brief Receive an element from a channel.
 *
 * @param ch   Channel handle.
 * @param out  Output buffer of at least elem_size bytes.
 * @return 0 on success, 1 if the channel is closed and empty,
 *         -1 on error.
 *
 * @note Copies elem_size bytes to @p out. On an empty channel,
 *       yields to the scheduler until data arrives or the channel
 *       is closed.
 */
int or_chan_recv(or_chan_t* ch, void* out);

/**
 * @brief Close a channel.
 *
 * @param ch Channel handle.
 * @return 0 on success, -1 if already closed or NULL.
 *
 * @note Unblocks all blocked senders (with -1) and receivers
 *       (with 1). Subsequent sends return -1.
 */
int or_chan_close(or_chan_t* ch);

/**
 * @brief Destroy a channel.
 *
 * @param ch Channel handle; NULL is a no-op.
 *
 * @warning Must not have any blocked senders/receivers.
 */
void or_chan_destroy(or_chan_t* ch);

/**
 * @brief Current number of buffered elements.
 *
 * @param ch Channel handle.
 * @return Element count, or 0 if @p ch is NULL.
 */
size_t or_chan_len(const or_chan_t* ch);

/**
 * @brief Channel capacity.
 *
 * @param ch Channel handle.
 * @return Capacity, or 0 if @p ch is NULL.
 */
size_t or_chan_cap(const or_chan_t* ch);

/**
 * @brief Is the channel closed?
 *
 * @param ch Channel handle.
 * @return 1 if closed, 0 if open or NULL.
 */
int or_chan_is_closed(const or_chan_t* ch);

#ifdef __cplusplus
}
#endif

#endif /* OL_OROUTINES_H */
