window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-async'] = {
    template: `
    <div class="ol-h1">Async helpers</div>
    <p class="ol-p">
    Schedule blocking or CPU-bound tasks on the pool, or post tasks on the event loop thread—both yielding a future.
    Destructors ensure safe cleanup when resolution fails.
    </p>

    <div class="ol-h2">API summary</div>
    <ul>
    <li><code>ol_async_run(pool, fn, arg, dtor)</code> → future</li>
    <li><code>ol_async_run_on_loop(loop, cb, arg, dtor)</code> → future</li>
    </ul>

    <div class="ol-h2">Examples</div>
    <div class="ol-tabs">
    <div class="ol-tab-buttons">
    <button class="ol-tab-button">Pool task</button>
    <button class="ol-tab-button">Loop callback</button>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Run on pool
    void* heavy_compute(void* arg) { /* do work */ return strdup("done"); }

    ol_future_t* fut = ol_async_run(pool, heavy_compute, NULL, free);
    int r = ol_future_await(fut, 0); // 1 on completion
    const char* v = (const char*)ol_future_get_value_const(fut);
    printf("%s\\n", v); // "done"
    ol_future_destroy(fut);
    </pre>
    </div>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Schedule on loop; callback may return a value or resolve later
    void* on_loop(ol_event_loop_t* lp, void* arg, ol_promise_t* p) {
        (void)lp; (void)arg; (void)p;
        return strdup("loop-result");
    }

    ol_future_t* fut = ol_async_run_on_loop(loop, on_loop, NULL, free);
    if (ol_future_await(fut, 0) == 1) {
        const char* v = (const char*)ol_future_get_value_const(fut);
        printf("%s\\n", v); // "loop-result"
    }
    ol_future_destroy(fut);
    </pre>
    </div>
    </div>
    </div>

    <div class="ol-h2">Notes</div>
    <ul>
    <li>Wrapper owns the promise; if fulfill fails, dtor frees the returned value.</li>
    <li>Loop-bound tasks use a tiny one-shot timer to ensure the loop polls before executing.</li>
    </ul>
    `,
    onMount() {}
};
