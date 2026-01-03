window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-semaphores'] = {
    template: `
    <div class="ol-h1">Semaphores</div>
    <p class="ol-p">
    Cross-platform counting semaphores (Windows HANDLE / POSIX sem_t) with try, timed wait,
    post, getvalue, and destroy semantics.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>ol_sem_init(s, initial, max_count)</code>, <code>ol_sem_destroy</code></li>
    <li><code>ol_sem_post</code>, <code>ol_sem_trywait</code></li>
    <li><code>ol_sem_wait_until(deadline_ns)</code> → 0 acquired, -3 timeout</li>
    <li><code>ol_sem_getvalue</code></li>
    </ul>
    `,
    onMount() {}
};
