window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-poller'] = {
    template: `
    <div class="ol-h1">Poller backend</div>
    <p class="ol-p">
    Abstracted event polling: epoll (Linux), kqueue (BSD/macOS), or select fallback. Tags identify callbacks.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>create / destroy</code></li>
    <li><code>add(fd, mask, tag)</code>, <code>mod(fd, mask, tag)</code>, <code>del(fd)</code></li>
    <li><code>wait(deadline, out_events, capacity)</code></li>
    </ul>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_poller_t* p = ol_poller_create();
    ol_deadline_t dl = ol_deadline_from_ms(100);
    ol_poll_event_t out[64];
    int n = ol_poller_wait(p, dl, out, 64);
    for(int i=0;i<n;i++){
        // dispatch by out[i].tag
    }
    ol_poller_destroy(p);
    </pre>
    </div>
    `,
    onMount() {}
};
