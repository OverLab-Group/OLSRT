window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-channel'] = {
    template: `
    <div class="ol-h1">Channel</div>
    <p class="ol-p">
    Thread-safe FIFO with optional capacity and destructor. Supports blocking/non-blocking send/recv,
    deadlines, close semantics, and introspection.
    </p>

    <div class="ol-h2">API summary</div>
    <ul>
    <li><code>create(cap, dtor)</code>, <code>destroy()</code>, <code>close()</code></li>
    <li><code>send / try_send / send_deadline</code></li>
    <li><code>recv / try_recv / recv_deadline</code></li>
    <li><code>len / capacity / is_closed</code></li>
    </ul>

    <div class="ol-h2">Ownership model</div>
    <div class="ol-callout">
    When sending, ownership transfers to the channel. If send fails due to closed channel,
    the destructor frees the item. On destroy, queued items are freed via the channel’s destructor.
    </div>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_channel_t* ch = ol_channel_create(/*unbounded*/0, free);

    char* a = strdup("A");
    char* b = strdup("B");
    ol_channel_send(ch, a);
    ol_channel_send_deadline(ch, b, ol_deadline_from_ms(100).when_ns);

    void* out;
    while (ol_channel_recv(ch, &out) == 1) {
        printf("%s\\n", (char*)out);
        free(out);
    }
    ol_channel_close(ch);
    ol_channel_destroy(ch);
    </pre>
    </div>
    `,
    onMount() {}
};
