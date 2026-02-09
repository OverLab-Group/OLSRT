window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-await'] = {
    template: `
    <div class="ol-h1">Await helpers</div>
    <p class="ol-p">
    Cooperative waiting inside an event loop thread without blocking the poller.
    Use short polling slices and wake the loop between checks.
    </p>

    <div class="ol-h2">API summary</div>
    <ul>
    <li><code>ol_await_future(f, deadline_ns)</code> → thin wrapper over <code>ol_future_await</code></li>
    <li><code>ol_await_future_with_loop(loop, f, deadline_ns)</code> → cooperative polling</li>
    </ul>

    <div class="ol-h2">Example</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // From loop thread, avoid blocking:
    int r = ol_await_future_with_loop(loop, fut, ol_deadline_from_ms(500).when_ns);
    if (r == 1) { /* completed */ }
    else if (r == -3) { /* timeout */ }
    else { /* error */ }
    </pre>
    </div>

    <div class="ol-h2">Guidance</div>
    <ul>
    <li>Only call <code>ol_await_future_with_loop</code> from the event loop thread.</li>
    <li>For other threads, use <code>ol_future_await</code>.</li>
    </ul>
    `,
    onMount() {}
};
