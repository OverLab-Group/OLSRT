/**
 * @file ol_dataflow.c
 * @brief Dataflow graph primitives: nodes, edges, and a simple worker pool to process items.
 *
 * Overview
 * --------
 * - Provides a lightweight dataflow graph where nodes have handlers and outbound ports.
 * - Edges connect node output ports to destination node inboxes (channels).
 * - Graph runs a set of worker tasks (via ol_parallel_pool) that poll node inboxes and invoke handlers.
 *
 * Design and ownership
 * --------------------
 * - Each node has a unified inbound inbox (channel) where edges push items.
 * - Edge inboxes may have per-edge destructors to control ownership semantics.
 * - Node handlers receive items and may emit via emit_impl which calls ol_df_emit.
 *
 * Concurrency model
 * -----------------
 * - Graph->pool executes worker tasks; workers iterate nodes and try to pull items non-blocking.
 * - This simple model favors low-latency processing but may busy-wait if no work is present.
 * - For production, integrate a wake/cond mechanism to avoid spinning.
 *
 * Testing and tooling
 * -------------------
 * - Unit tests should validate connect/disconnect, push/emit semantics, ownership and destructor calls,
 *   and start/stop lifecycle.
 * - Use ASan/Valgrind to detect leaks; TSan to detect races when handlers access shared state.
 * - Fuzz harnesses can exercise graph topology mutations and message flows.
 */

#include "ol_dataflow.h"

#include <stdlib.h>
#include <string.h>

/* -------------------- Internal structures -------------------- */

/**
 * @struct df_inbox
 * @brief Per-edge or per-node inbox wrapper.
 *
 * Fields:
 * - ch: underlying channel used to queue inbound items
 * - dtor: destructor for items in this inbox (NULL => forward-only ownership)
 */
typedef struct df_inbox {
    ol_channel_t *ch;
    ol_df_item_destructor dtor;
} df_inbox_t;

struct ol_df_edge {
    ol_df_node_t *from;
    int            from_port;
    ol_df_node_t  *to;
    df_inbox_t     inbox;          /* dst inbox channel and destructor */
    struct ol_df_edge *next;       /* linking in graph list */
};

struct ol_df_node {
    ol_df_graph_t   *graph;
    ol_df_handler     handler;
    void             *user_ctx;

    int               out_ports;
    struct ol_df_edge **outs;      /* outs[port] -> linked list head */

    df_inbox_t        self_inbox;  /* unified inbound inbox for node */

    struct ol_df_node *next;       /* linking in graph list */
};

struct ol_df_graph {
    ol_parallel_pool_t *pool;

    ol_df_node_t *nodes_head;
    ol_df_edge_t *edges_head;

    ol_mutex_t mu;

    bool running;
    size_t node_count;
    size_t edge_count;
};

/* -------------------- Utilities -------------------- */

/**
 * @brief Create an inbox wrapper with a channel of given capacity and destructor.
 *
 * @param capacity Channel capacity (0 = unbounded)
 * @param dtor Destructor for items (may be NULL)
 * @return df_inbox_t created inbox (ch may be NULL on failure)
 */
static df_inbox_t df_inbox_create(size_t capacity, ol_df_item_destructor dtor) {
    df_inbox_t ib;
    ib.ch = ol_channel_create(capacity, dtor);
    ib.dtor = dtor;
    return ib;
}

/**
 * @brief Destroy inbox: destroy underlying channel if present.
 *
 * @param ib Pointer to inbox
 */
static void df_inbox_destroy(df_inbox_t *ib) {
    if (!ib) return;
    if (ib->ch) {
        ol_channel_destroy(ib->ch);
        ib->ch = NULL;
    }
}

/* Graph registry helpers (assume caller holds graph mutex when needed) */

static void graph_enqueue_edge(ol_df_graph_t *g, ol_df_edge_t *e) {
    e->next = g->edges_head;
    g->edges_head = e;
    g->edge_count++;
}

static void graph_enqueue_node(ol_df_graph_t *g, ol_df_node_t *n) {
    n->next = g->nodes_head;
    g->nodes_head = n;
    g->node_count++;
}

static void graph_remove_edge(ol_df_graph_t *g, ol_df_edge_t *e) {
    ol_df_edge_t **pp = &g->edges_head;
    while (*pp) {
        if (*pp == e) { *pp = e->next; g->edge_count--; break; }
        pp = &(*pp)->next;
    }
}

static void graph_remove_node(ol_df_graph_t *g, ol_df_node_t *n) {
    ol_df_node_t **pp = &g->nodes_head;
    while (*pp) {
        if (*pp == n) { *pp = n->next; g->node_count--; break; }
        pp = &(*pp)->next;
    }
}

/**
 * @brief Helper used by node handlers to emit items on a port.
 *
 * This function simply forwards to ol_df_emit for the node.
 *
 * @param ctx Node pointer (ol_df_node_t*)
 * @param port_index Output port index
 * @param out_item Item to emit (ownership semantics follow ol_df_emit)
 * @return int status (0 on success)
 */
static int emit_impl(void *ctx, int port_index, void *out_item) {
    ol_df_node_t *from = (ol_df_node_t*)ctx;
    return ol_df_emit(from, port_index, out_item);
}

/* -------------------- Worker function -------------------- */

/**
 * @brief Worker loop executed on pool threads.
 *
 * Behavior:
 * - While graph running, iterate nodes and try to pull one item from each node's self_inbox.
 * - If an item is obtained, invoke node->handler(user_ctx, item, emit_impl, node).
 * - This simple implementation uses ol_channel_try_recv to avoid blocking a worker on one node.
 *
 * Notes:
 * - For production, consider a more efficient scheduling/wakeup mechanism to avoid spinning.
 *
 * @param arg ol_df_graph_t* pointer
 */
static void df_worker(void *arg) {
    ol_df_graph_t *g = (ol_df_graph_t*)arg;

    for (;;) {
        /* Stop condition: graph not running */
        ol_mutex_lock(&g->mu);
        bool running = g->running;
        ol_mutex_unlock(&g->mu);
        if (!running) break;

        /* Iterate nodes and try to pull one item to process */
        ol_df_node_t *n = g->nodes_head;
        bool did_work = false;

        while (n) {
            void *item = NULL;
            int got = ol_channel_try_recv(n->self_inbox.ch, &item);
            if (got == 1) {
                did_work = true;
                /* Process item via node handler */
                if (n->handler) {
                    (void)n->handler(n->user_ctx, item, emit_impl, n);
                }
                /* Responsibility for freeing/forwarding item is with handler or inbox destructor semantics */
            }

            /* Note: inbound edges feed into node->self_inbox; no per-edge recv here */
            n = n->next;
        }

        /* If no work performed, yield briefly (could be improved with cond/wakeup) */
        if (!did_work) {
            /* Cooperative small pause: in a real implementation, use a condition or event to wait */
        }
    }
}

/* -------------------- Public API -------------------- */

/**
 * @brief Create a dataflow graph with a parallel pool of num_threads workers.
 *
 * @param num_threads Number of worker threads (0 => 1)
 * @return ol_df_graph_t* New graph or NULL on failure
 */
ol_df_graph_t* ol_df_graph_create(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;

    ol_df_graph_t *g = (ol_df_graph_t*)calloc(1, sizeof(ol_df_graph_t));
    if (!g) return NULL;

    g->pool = ol_parallel_create(num_threads);
    if (!g->pool) { free(g); return NULL; }

    g->nodes_head = NULL;
    g->edges_head = NULL;
    g->running = false;
    g->node_count = 0;
    g->edge_count = 0;
    ol_mutex_init(&g->mu);

    return g;
}

/**
 * @brief Destroy graph: stop, destroy edges and nodes, and free resources.
 *
 * @param g Graph pointer
 */
void ol_df_graph_destroy(ol_df_graph_t *g) {
    if (!g) return;

    (void)ol_df_graph_stop(g);

    /* Destroy edges (inboxes) */
    ol_df_edge_t *e = g->edges_head;
    while (e) {
        ol_df_edge_t *nx = e->next;
        df_inbox_destroy(&e->inbox);
        free(e);
        e = nx;
    }
    g->edges_head = NULL;
    g->edge_count = 0;

    /* Destroy nodes */
    ol_df_node_t *n = g->nodes_head;
    while (n) {
        ol_df_node_t *nx = n->next;
        df_inbox_destroy(&n->self_inbox);
        if (n->outs) free(n->outs);
        free(n);
        n = nx;
    }
    g->nodes_head = NULL;
    g->node_count = 0;

    ol_mutex_destroy(&g->mu);
    ol_parallel_destroy(g->pool);
    free(g);
}

/**
 * @brief Create a node in the graph.
 *
 * - Each node gets a unified inbound inbox (unbounded by default).
 * - out_ports specifies number of outbound ports (fanout lists).
 *
 * @param g Graph pointer
 * @param handler Node handler function (may be NULL)
 * @param user_ctx Opaque user context passed to handler
 * @param out_ports Number of outbound ports (>=0)
 * @return ol_df_node_t* New node or NULL on failure
 */
ol_df_node_t* ol_df_node_create(ol_df_graph_t *g, ol_df_handler handler, void *user_ctx, int out_ports) {
    if (!g || out_ports < 0) return NULL;

    ol_df_node_t *n = (ol_df_node_t*)calloc(1, sizeof(ol_df_node_t));
    if (!n) return NULL;

    n->graph = g;
    n->handler = handler;
    n->user_ctx = user_ctx;
    n->out_ports = out_ports;
    n->outs = NULL;
    n->next = NULL;

    /* One unified inbound inbox per node; edges feed into this channel.
     * Capacity chosen as unbounded; backpressure is controlled at edges via their capacities.
     */
    n->self_inbox = df_inbox_create(/*unbounded*/0, /*node doesn’t own input items*/ NULL);
    if (!n->self_inbox.ch) { free(n); return NULL; }

    if (out_ports > 0) {
        n->outs = (ol_df_edge_t**)calloc(out_ports, sizeof(ol_df_edge_t*));
        if (!n->outs) { df_inbox_destroy(&n->self_inbox); free(n); return NULL; }
    }

    ol_mutex_lock(&g->mu);
    graph_enqueue_node(g, n);
    ol_mutex_unlock(&g->mu);

    return n;
}

/**
 * @brief Remove a node from graph. Node must be disconnected (no outbound edges).
 *
 * @param g Graph pointer
 * @param n Node pointer
 * @return int 0 on success, -1 on error
 */
int ol_df_node_remove(ol_df_graph_t *g, ol_df_node_t *n) {
    if (!g || !n) return -1;

    /* Must be disconnected: check outs */
    for (int i = 0; i < n->out_ports; i++) {
        if (n->outs && n->outs[i]) return -1; /* still connected */
    }

    ol_mutex_lock(&g->mu);
    graph_remove_node(g, n);
    ol_mutex_unlock(&g->mu);

    df_inbox_destroy(&n->self_inbox);
    if (n->outs) free(n->outs);
    free(n);
    return 0;
}

/**
 * @brief Connect an edge from a node's output port to a destination node.
 *
 * - Creates an edge with its own inbox channel (capacity and destructor).
 * - Adds edge to graph registry and to from->outs list.
 *
 * @param g Graph pointer
 * @param from Source node
 * @param src_port Source port index
 * @param to Destination node
 * @param capacity Edge inbox capacity
 * @param dtor Destructor for items on this edge (may be NULL)
 * @return ol_df_edge_t* New edge or NULL on failure
 */
ol_df_edge_t* ol_df_connect(ol_df_graph_t *g,
                            ol_df_node_t *from, int src_port,
                            ol_df_node_t *to,
                            size_t capacity,
                            ol_df_item_destructor dtor)
{
    if (!g || !from || !to) return NULL;
    if (src_port < 0 || src_port >= from->out_ports) return NULL;

    ol_df_edge_t *e = (ol_df_edge_t*)calloc(1, sizeof(ol_df_edge_t));
    if (!e) return NULL;

    e->from = from;
    e->from_port = src_port;
    e->to = to;
    e->inbox = df_inbox_create(capacity, dtor);
    if (!e->inbox.ch) { free(e); return NULL; }

    /* Link edge in graph registry and into from->outs list (push-front) */
    ol_mutex_lock(&g->mu);
    graph_enqueue_edge(g, e);
    e->next = from->outs[src_port];
    from->outs[src_port] = e;
    ol_mutex_unlock(&g->mu);

    return e;
}

/**
 * @brief Disconnect and destroy an edge.
 *
 * @param g Graph pointer
 * @param e Edge pointer
 * @return int 0 on success, -1 on error
 */
int ol_df_disconnect(ol_df_graph_t *g, ol_df_edge_t *e) {
    if (!g || !e) return -1;

    ol_df_node_t *from = e->from;

    ol_mutex_lock(&g->mu);
    /* Remove from from->outs list */
    ol_df_edge_t **pp = &from->outs[e->from_port];
    while (*pp) {
        if (*pp == e) { *pp = e->next; break; }
        pp = &(*pp)->next;
    }
    /* Remove from graph list */
    graph_remove_edge(g, e);
    ol_mutex_unlock(&g->mu);

    df_inbox_destroy(&e->inbox);
    free(e);
    return 0;
}

/**
 * @brief Start graph processing: submit worker tasks equal to pool thread count.
 *
 * @param g Graph pointer
 * @return int 0 on success, -1 on invalid arg
 */
int ol_df_graph_start(ol_df_graph_t *g) {
    if (!g) return -1;
    ol_mutex_lock(&g->mu);
    if (g->running) { ol_mutex_unlock(&g->mu); return 0; }
    g->running = true;
    ol_mutex_unlock(&g->mu);

    /* Submit N worker tasks equal to pool size */
    size_t workers = ol_parallel_thread_count(g->pool);
    if (workers == 0) workers = 1;
    for (size_t i = 0; i < workers; i++) {
        (void)ol_parallel_submit(g->pool, df_worker, g);
    }
    return 0;
}

/**
 * @brief Stop graph processing: mark not running and flush pool to let workers exit.
 *
 * @param g Graph pointer
 * @return int 0 on success, -1 on invalid arg
 */
int ol_df_graph_stop(ol_df_graph_t *g) {
    if (!g) return -1;
    ol_mutex_lock(&g->mu);
    if (!g->running) { ol_mutex_unlock(&g->mu); return 0; }
    g->running = false;
    ol_mutex_unlock(&g->mu);

    /* Flush pool to let workers exit their loops */
    (void)ol_parallel_flush(g->pool);
    return 0;
}

/**
 * @brief Push an item directly into a node's unified inbox (source injection).
 *
 * - Node does not own items by default; ownership semantics depend on channel destructors.
 *
 * @param g Graph pointer
 * @param to Destination node
 * @param item Item pointer
 * @return int 0 on success, -1 on error
 */
int ol_df_push(ol_df_graph_t *g, ol_df_node_t *to, void *item) {
    if (!g || !to) return -1;
    return ol_channel_send(to->self_inbox.ch, item);
}

/**
 * @brief Emit an item from a node on a given output port.
 *
 * - Fan-out: send to each connected edge's inbox channel.
 * - If an edge owns items (inbox.dtor != NULL) and send fails due to closed channel,
 *   the item is freed via the edge's destructor to avoid leaks.
 *
 * @param from Source node
 * @param port_index Output port index
 * @param item Item pointer (ownership semantics: caller retains ownership until send succeeds)
 * @return int 0 on success (attempted sends), -1 on invalid args
 */
int ol_df_emit(ol_df_node_t *from, int port_index, void *item) {
    if (!from || port_index < 0 || port_index >= from->out_ports) return -1;

    /* Fan out: send to each connected edge's inbox channel */
    ol_df_edge_t *e = from->outs[port_index];
    for (; e; e = e->next) {
        int r = ol_channel_send(e->inbox.ch, item);
        if (r < 0) {
            /* If edge owns items and send failed due to closed channel, free */
            if (e->inbox.dtor) e->inbox.dtor(item);
            /* Continue to other edges; report first error */
        }
    }
    return 0;
}

/* -------------------- Introspection -------------------- */

size_t ol_df_node_out_ports(const ol_df_node_t *n) {
    return n ? (size_t)n->out_ports : 0;
}

size_t ol_df_graph_node_count(const ol_df_graph_t *g) {
    return g ? g->node_count : 0;
}

size_t ol_df_graph_edge_count(const ol_df_graph_t *g) {
    return g ? g->edge_count : 0;
}

/**
 * @brief Check whether graph is running.
 *
 * @param g Graph pointer
 * @return bool true if running, false otherwise
 */
bool ol_df_graph_is_running(const ol_df_graph_t *g) {
    if (!g) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&g->mu);
    r = g->running;
    ol_mutex_unlock((ol_mutex_t*)&g->mu);
    return r;
}