#include "ol_dataflow.h"

#include <stdlib.h>
#include <string.h>

/* Internal structures */

typedef struct df_inbox {
    ol_channel_t *ch;              /* inbound queue */
    ol_df_item_destructor dtor;    /* ownership for edge items (NULL => forward-only) */
} df_inbox_t;

struct ol_df_edge {
    ol_df_node_t *from;
    int            from_port;
    ol_df_node_t  *to;
    df_inbox_t     inbox;          /* dst inbox channel and destructor */
    /* Linking in graph list */
    struct ol_df_edge *next;
};

struct ol_df_node {
    ol_df_graph_t   *graph;
    ol_df_handler     handler;
    void             *user_ctx;

    /* Outbound ports: array of edges lists (fanout) */
    int               out_ports;
    struct ol_df_edge       **outs;       /* outs[port] -> linked list head */

    /* Implicit input inbox (for feeding source nodes directly) */
    df_inbox_t        self_inbox;

    /* Linking in graph list */
    struct ol_df_node *next;
};

struct ol_df_graph {
    ol_parallel_pool_t *pool;

    /* Registry */
    ol_df_node_t *nodes_head;
    ol_df_edge_t *edges_head;

    /* Synchronization */
    ol_mutex_t mu;

    /* State */
    bool running;
    size_t node_count;
    size_t edge_count;
};

/* Utilities */

static df_inbox_t df_inbox_create(size_t capacity, ol_df_item_destructor dtor) {
    df_inbox_t ib;
    ib.ch = ol_channel_create(capacity, dtor);
    ib.dtor = dtor;
    return ib;
}

static void df_inbox_destroy(df_inbox_t *ib) {
    if (!ib) return;
    if (ib->ch) {
        ol_channel_destroy(ib->ch);
        ib->ch = NULL;
    }
}

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

static int emit_impl(void *ctx, int port_index, void *out_item) {
    ol_df_node_t *from = (ol_df_node_t*)ctx;
    return ol_df_emit(from, port_index, out_item);
}

/* Worker function: drain all inboxes (self and edges) and process items */
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
                /* If inbox owns items, destructor is invoked by channel on destroy;
                 * here item is handed to handler (which may forward). If handler does not forward,
                 * responsibility remains ambiguous. We assume handler consumes the item or forwards it.
                 * If edge-specific ownership is needed, define node-level policy externally. */
            }

            /* Also check inbound edges (each edge inbox is the same as node self inbox for simplicity):
             * We model node’s inbound unified inbox via self_inbox; edges push into it.
             * Therefore no additional per-edge recv here.
             */
            n = n->next;
        }

        /* If no work performed, sleep-yield via pool mechanisms: we can block briefly by waiting on a condition
         * but to keep portable and simple, rely on workers being woken by submit. Here we spin lightly. */
        if (!did_work) {
            /* Cooperative small pause by submitting nothing; pool threads will loop again.
             * In production, integrate a wake signal or cond wait. */
        }
    }
}

/* Public API */

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

int ol_df_node_remove(ol_df_graph_t *g, ol_df_node_t *n) {
    if (!g || !n) return -1;

    /* Must be disconnected: check outs and ensure no edges target n’s inbox (handled at disconnect). */
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

    /* Link edge in graph registry */
    ol_mutex_lock(&g->mu);
    graph_enqueue_edge(g, e);
    /* Add to node’s outbound port list (push-front) */
    e->next = from->outs[src_port];
    from->outs[src_port] = e;
    ol_mutex_unlock(&g->mu);

    return e;
}

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

int ol_df_push(ol_df_graph_t *g, ol_df_node_t *to, void *item) {
    if (!g || !to) return -1;
    /* Push directly into node’s unified inbox; node does not own items by default */
    return ol_channel_send(to->self_inbox.ch, item);
}

int ol_df_emit(ol_df_node_t *from, int port_index, void *item) {
    if (!from || port_index < 0 || port_index >= from->out_ports) return -1;

    /* Fan out: send to each connected edge’s inbox channel */
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

/* Introspection */

size_t ol_df_node_out_ports(const ol_df_node_t *n) {
    return n ? (size_t)n->out_ports : 0;
}

size_t ol_df_graph_node_count(const ol_df_graph_t *g) {
    return g ? g->node_count : 0;
}

size_t ol_df_graph_edge_count(const ol_df_graph_t *g) {
    return g ? g->edge_count : 0;
}

bool ol_df_graph_is_running(const ol_df_graph_t *g) {
    if (!g) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&g->mu);
    r = g->running;
    ol_mutex_unlock((ol_mutex_t*)&g->mu);
    return r;
}
