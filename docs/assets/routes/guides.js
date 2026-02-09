window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['guides'] = {
    template: `
    <div class="ol-h1">Concepts and Guides</div>
    <p class="ol-p">
    A practical, concept-first journey through the runtime’s primitives.
    </p>

    <div class="ol-h2">Ownership and memory model</div>
    <div class="ol-callout">
    Mailbox items (channels) follow authoritative destructors. When sending, ownership transfers.
    Behaviors and handlers must explicitly consume or release items, avoiding leaks and double frees.
    </div>

    <div class="ol-h2">Thread-safety and synchronization</div>
    <p class="ol-p">
    Public APIs are guarded by mutexes/conditions. Higher-level contracts (e.g., actor behaviors, stream ops)
    assume cooperative discipline—no blocking on loop threads, clean cancellation, and proper lifecycle.
    </p>

    <div class="ol-h2">Monotonic time, deadlines, and timers</div>
    <p class="ol-p">
    Deadlines use monotonic clock to avoid wall-clock jumps. Poller wait and event loop scheduling clamp long
    timeouts defensively. Prefer deadlines over sleeping whenever coordination is needed.
    </p>

    <div class="ol-h2">Resiliency: supervisors</div>
    <p class="ol-p">
    Supervisors manage child lifecycles and restart strategies (one-for-one, one-for-all, rest-for-one) with
    intensity windows. They react to non-zero exits and permanent policies.
    </p>

    <div class="ol-h2">Composability: dataflow, reactive, streams</div>
    <p class="ol-p">
    Dataflow nodes and edges move items via channels. Reactive/streams provide backpressure with buffered queues,
    demand mechanics, and operators (map, filter, take, debounce, merge). Combine loop timers and IO sources to build pipelines.
    </p>
    `,
    onMount() {}
};
