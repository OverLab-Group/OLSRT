#ifndef OL_DATAFLOW_H
#define OL_DATAFLOW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_lock_mutex.h"
#include "ol_parallel.h"
#include "ol_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_df_graph  ol_df_graph_t;
typedef struct ol_df_node   ol_df_node_t;
typedef struct ol_df_edge   ol_df_edge_t;

/* Item destructor used by edges */
typedef void (*ol_df_item_destructor)(void *item);

/* Node processing callback:
 * - ctx: user node context set at node creation
 * - in_item: consumed input item (may be NULL for tick sources)
 * - emit: function to send zero or more outputs to specific outbound port index
 *         Returns 0 on success, negative on failure
 * Handlers MUST be re-entrant if graph runs with >1 worker; avoid shared mutable state.
 */
typedef int (*ol_df_handler)(
    void *ctx,
    void *in_item,
    int (*emit)(void *ctx, int port_index, void *out_item),
    void *emit_ctx
);

/* Create an empty graph with a worker pool (num_threads>=1).
 * If num_threads==0, creates a single worker thread.
 */
ol_df_graph_t* ol_df_graph_create(size_t num_threads);

/* Destroy graph: stops workers, closes edges, frees nodes and edges. */
void ol_df_graph_destroy(ol_df_graph_t *g);

/* Create a node:
 * - handler: node function to process inbound items
 * - user_ctx: arbitrary pointer passed to handler
 * - out_ports: number of outbound ports this node exposes (>=0)
 * Returns node or NULL on failure.
 */
ol_df_node_t* ol_df_node_create(ol_df_graph_t *g, ol_df_handler handler, void *user_ctx, int out_ports);

/* Remove a node (must be disconnected). Returns 0 on success. */
int ol_df_node_remove(ol_df_graph_t *g, ol_df_node_t *n);

/* Connect nodes:
 * - from (src node), src_port: outbound port index of src (0..out_ports-1)
 * - to (dst node): receives items via a bounded channel
 * - capacity: channel capacity (0 => unbounded)
 * - dtor: optional destructor applied by edge when it owns items
 * Returns edge handle or NULL on failure.
 */
ol_df_edge_t* ol_df_connect(ol_df_graph_t *g,
                            ol_df_node_t *from, int src_port,
                            ol_df_node_t *to,
                            size_t capacity,
                            ol_df_item_destructor dtor);

/* Disconnect an edge and free it. Returns 0 on success. */
int ol_df_disconnect(ol_df_graph_t *g, ol_df_edge_t *e);

/* Start the graph: launches worker threads that drain node inboxes and call handlers. */
int ol_df_graph_start(ol_df_graph_t *g);

/* Stop the graph: gracefully drain current work and stop workers. */
int ol_df_graph_stop(ol_df_graph_t *g);

/* Push an item directly into a node’s implicit input (source node).
 * Typically used to feed source nodes; returns 0 success, -3 timeout (never here), -1 error.
 */
int ol_df_push(ol_df_graph_t *g, ol_df_node_t *to, void *item);

/* Emit helper for node handlers: send item to outbound port (0..out_ports-1).
 * Provided to handlers via the emit function pointer argument; exposed here for manual use.
 */
int ol_df_emit(ol_df_node_t *from, int port_index, void *item);

/* Introspection */
size_t ol_df_node_out_ports(const ol_df_node_t *n);
size_t ol_df_graph_node_count(const ol_df_graph_t *g);
size_t ol_df_graph_edge_count(const ol_df_graph_t *g);
bool   ol_df_graph_is_running(const ol_df_graph_t *g);

#ifdef __cplusplus
}
#endif

#endif /* OL_DATAFLOW_H */
