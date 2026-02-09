window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-dataflow'] = {
    template: `
    <div class="ol-h1">Dataflow</div>
    <p class="ol-p">
    Lightweight dataflow graph. Nodes handle items, edges connect ports with inbox channels;
    worker tasks poll node inboxes non-blocking and invoke handlers.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>graph_create / destroy / start / stop</code></li>
    <li><code>node_create(handler, user_ctx, out_ports)</code></li>
    <li><code>connect(from, port, to, capacity, dtor)</code>, <code>disconnect(edge)</code></li>
    <li><code>push(to, item)</code>, <code>emit(from, port, item)</code></li>
    </ul>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_df_graph_t* g = ol_df_graph_create(2);

    int handler(void* ctx, void* item, int (*emit)(void*,int,void*), void* node){
        // process item and emit on port 0
        emit(node, 0, item);
        return 0;
    }

    ol_df_node_t* n1 = ol_df_node_create(g, handler, NULL, 1);
    ol_df_node_t* n2 = ol_df_node_create(g, handler, NULL, 1);
    ol_df_edge_t* e = ol_df_connect(g, n1, 0, n2, /*capacity*/128, /*dtor*/free);

    ol_df_graph_start(g);
    ol_df_push(g, n1, strdup("x"));

    // Clean up
    ol_df_graph_stop(g);
    ol_df_disconnect(g, e);
    ol_df_node_remove(g, n2);
    ol_df_node_remove(g, n1);
    ol_df_graph_destroy(g);
    </pre>
    </div>

    <div class="ol-h2">Notes</div>
    <ul>
    <li>The simple worker loop may busy-wait if no work is present; integrate wakes for production.</li>
    <li>Edge inbox destructor controls item ownership semantics on send failures.</li>
    </ul>
    `,
    onMount() {}
};
