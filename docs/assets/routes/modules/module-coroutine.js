window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-coroutines'] = {
    template: `
    <div class="ol-h1">Coroutines</div>
    <p class="ol-p">
    Cooperative coroutines on a single-threaded scheduler, backed by platform-specific green threads.
    Supports spawn/resume/yield/join/cancel, with payload exchange under mutex protection.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>ol_coroutine_scheduler_init / shutdown</code></li>
    <li><code>ol_co_spawn(entry, arg, stack_size)</code></li>
    <li><code>ol_co_resume(co, payload)</code>, <code>ol_co_yield(payload)</code></li>
    <li><code>ol_co_join(co)</code>, <code>ol_co_cancel(co)</code>, <code>ol_co_destroy(co)</code></li>
    </ul>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    int entry_fn(void* arg){
        void* from_caller = ol_co_yield(strdup("hello"));
        // process payload
        return 0;
    }

    ol_coroutine_scheduler_init();
    ol_co_t* co = ol_co_spawn(entry_fn, NULL, 128*1024);

    // first resume; coroutine yields "hello"
    ol_co_resume(co, NULL);

    // join cooperatively
    ol_co_join(co);
    ol_co_destroy(co);
    ol_coroutine_scheduler_shutdown();
    </pre>
    </div>
    `,
    onMount() {}
};
