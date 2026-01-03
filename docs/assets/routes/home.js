window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['home'] = {
    template: `
    <div class="ol-card">
    <div class="ol-h1">Welcome to OverLab Runtime</div>
    <p class="ol-p">
    This documentation teaches you the full stack: Actor model, Channels, Promises/Futures, Event Loop, Poller
    backends, Parallel pools, Coroutines & Green Threads, Reactive and Streams, Semaphores, Supervisor trees,
    and Dataflow graphs—built for clarity, performance, and composability.
    </p>
    </div>

    <div class="ol-grid cols-3" style="margin-top:16px;">
    <div class="ol-card">
    <div class="ol-h2">Lightning Quickstart</div>
    <p class="ol-p">Spin up the event loop, schedule a timer, send messages through channels, and resolve promises.</p>
    <button class="ol-tab-button" onclick="OL_GOTO('getting-started')">Start</button>
    </div>
    <div class="ol-card">
    <div class="ol-h2">Learn by Modules</div>
    <p class="ol-p">Explore modules individually with interactive snippets and API references.</p>
    <button class="ol-tab-button" onclick="OL_GOTO('modules')">Browse modules</button>
    </div>
    <div class="ol-card">
    <div class="ol-h2">Testing & Tooling</div>
    <p class="ol-p">Get practical guidance on sanitizers, fuzzing, and performance benchmarks.</p>
    <button class="ol-tab-button" onclick="OL_GOTO('testing')">Go</button>
    </div>
    </div>

    <div class="ol-collapse" style="margin-top:16px;">
    <div class="ol-collapse-header">
    <div><span class="ol-badge">New</span> Philosophy and design notes</div>
    <div>▾</div>
    </div>
    <div class="ol-collapse-body">
    <p class="ol-p">
    OverLab favors explicit ownership semantics, monotonic time domains, and defensive patterns against
    concurrent races. Modules interlock: channels for data movement, promises for async results, event loops
    to drive callbacks, and supervisors to maintain system resiliency.
    </p>
    </div>
    </div>
    `,
    onMount() {}
};
