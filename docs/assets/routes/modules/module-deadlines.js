window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-deadlines'] = {
    template: `
    <div class="ol-h1">Deadlines</div>
    <p class="ol-p">
    Monotonic time utilities, deadline creation, expiration checks, remaining time queries,
    sleep helpers, and poll timeout clamping.
    </p>

    <div class="ol-h2">API</div>
    <ul>
    <li><code>ol_monotonic_now_ns()</code></li>
    <li><code>ol_deadline_from_ns/ms/sec</code>, <code>ol_deadline_expired</code></li>
    <li><code>ol_deadline_remaining_ns/ms</code>, <code>ol_sleep_until</code></li>
    <li><code>ol_clamp_poll_timeout_ms</code></li>
    </ul>
    `,
    onMount() {}
};
