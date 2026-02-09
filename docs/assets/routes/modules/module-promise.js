window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-promise'] = {
    template: `
    <div class="ol-h1">Promise / Future</div>
    <p class="ol-p">
    Resolution, continuations, and value ownership (with destructor semantics). Loop-aware waking for
    continuations ensures progress on loop-bound workflows.
    </p>

    <div class="ol-h2">API summary</div>
    <ul>
    <li><code>ol_promise_create(loop?)</code>, <code>get_future</code>, <code>destroy</code></li>
    <li><code>fulfill(value, dtor)</code>, <code>reject(code)</code>, <code>cancel()</code></li>
    <li><code>ol_future_await(deadline_ns)</code>, <code>ol_future_then(cb)</code></li>
    <li><code>get_value_const</code> vs <code>take_value</code> (ownership transfer)</li>
    </ul>

    <div class="ol-h2">Example: continuations</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_promise_t* p = ol_promise_create(loop);
    ol_future_t* f = ol_promise_get_future(p);

    ol_future_then(f, [](ol_event_loop_t* lp, ol_promise_state_t st, const void* v, int err, void* ud){
        (void)lp; (void)ud;
        if (st == OL_PROMISE_FULFILLED) printf("value: %s\\n", (const char*)v);
        else if (st == OL_PROMISE_REJECTED) printf("error: %d\\n", err);
    }, NULL);

        ol_promise_fulfill(p, strdup("Hi"), free);
        // cleanup after resolution
        ol_future_destroy(f);
        ol_promise_destroy(p);
        </pre>
        </div>
        `,
        onMount() {}
};
