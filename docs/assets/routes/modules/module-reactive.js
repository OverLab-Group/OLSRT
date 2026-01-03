window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-reactive'] = {
    template: `
    <div class="ol-h1">Reactive</div>
    <p class="ol-p">
    Observables with backpressure (demand), buffered queues, and operators like map, filter, take, debounce, merge.
    Loop-integrated IO and timer sources.
    </p>

    <div class="ol-h2">API highlights</div>
    <ul>
    <li><code>ol_subject_create(loop, dtor)</code>, <code>ol_observable_subscribe(..., demand)</code></li>
    <li><code>ol_subject_on_next / on_error / on_complete</code></li>
    <li>Operators: <code>ol_rx_map / filter / take / debounce / merge</code></li>
    <li>Sources: <code>ol_rx_timer(loop, period_ns, count)</code>, <code>ol_rx_from_fd(loop, fd, mask)</code></li>
    </ul>

    <div class="ol-h2">Example: debounce</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_observable_t* src = ol_observable_create(loop, free);
    ol_observable_t* deb = ol_rx_debounce(src, /*interval_ns*/ 200*1000000);
    ol_rx_subscription_t* sub = ol_observable_subscribe(deb,
                                                        [](void* item, void* ud){ printf("debounced item\\n"); }, NULL, NULL, 1, NULL);

    // Emit rapidly; only the last item per interval will pass
    ol_subject_t* subj = (ol_subject_t*)malloc(sizeof(ol_subject_t)); // typically created via subject_create
    // (Use subject_create in real code)
    </pre>
    </div>
    `,
    onMount() {}
};
