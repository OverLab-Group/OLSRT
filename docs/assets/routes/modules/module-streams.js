window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-streams'] = {
    template: `
    <div class="ol-h1">Streams</div>
    <p class="ol-p">
    Stream variant of reactive patterns: subscribe with demand, buffer items, and compose via operators.
    IO/timer sources are available.
    </p>

    <div class="ol-h2">API highlights</div>
    <ul>
    <li><code>ol_stream_create(loop, dtor)</code>, <code>ol_stream_subscribe(..., demand)</code></li>
    <li><code>emit_next / emit_error / emit_complete</code></li>
    <li>Operators: <code>map / filter / take / debounce / merge</code></li>
    <li>Sources: <code>ol_stream_timer</code>, <code>ol_stream_from_fd</code></li>
    </ul>
    `,
    onMount() {}
};
