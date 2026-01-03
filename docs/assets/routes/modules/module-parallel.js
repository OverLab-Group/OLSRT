window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-parallel'] = {
    template: `
    <div class="ol-h1">Parallel pool</div>
    <p class="ol-p">
    Thread pool with a FIFO queue, worker threads, idle flush, and shutdown semantics.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>create(num_threads)</code>, <code>destroy()</code></li>
    <li><code>submit(fn, arg)</code>, <code>flush()</code>, <code>shutdown(drain)</code></li>
    <li><code>thread_count / queue_size / is_running</code></li>
    </ul>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    ol_parallel_pool_t* p = ol_parallel_create(4);
    ol_parallel_submit(p, [](void* arg){ printf("Task\\n"); }, NULL);
    ol_parallel_flush(p);
    ol_parallel_shutdown(p, true);
    ol_parallel_destroy(p);
    </pre>
    </div>
    `,
    onMount() {}
};
