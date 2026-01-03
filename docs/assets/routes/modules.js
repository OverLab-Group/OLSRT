window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['modules'] = {
    template: `
    <div class="ol-h1">Modules</div>
    <p class="ol-p">Explore each module with focused examples, best practices, and API summaries.</p>

    <div class="ol-grid cols-3" style="margin-top: 12px;">
    ${[
        ['Actor model', 'module-actor', 'Mailbox + behaviors + ask/reply envelopes.'],
        ['Async helpers', 'module-async', 'Run tasks on pool or loop and get futures.'],
        ['Await helpers', 'module-await', 'Cooperative waiting without blocking loop.'],
        ['Channel', 'module-channel', 'Thread-safe FIFO with deadlines and try semantics.'],
        ['Event loop', 'module-event-loop', 'Portable timers + IO + wake pipe integration.'],
        ['Poller backend', 'module-poller', 'epoll/kqueue/select backends, tag-based events.'],
        ['Promise/Future', 'module-promise', 'Resolution, continuations, value ownership.'],
        ['Coroutines', 'module-coroutines', 'Cooperative scheduler over green threads.'],
        ['Green threads', 'module-green-threads', 'Windows Fibers / POSIX ucontext.'],
        ['Parallel pool', 'module-parallel', 'Task queue, workers, flush & shutdown.'],
        ['Deadlines', 'module-deadlines', 'Monotonic time, sleep & clamps.'],
        ['Reactive', 'module-reactive', 'Observables, subs, backpressure, operators.'],
        ['Streams', 'module-streams', 'Stream API variant of reactive primitives.'],
        ['Semaphores', 'module-semaphores', 'Cross-platform counting semaphores.'],
        ['Supervisor', 'module-supervisor', 'Process trees, restart strategies.'],
        ['Dataflow', 'module-dataflow', 'Nodes, edges, worker processing.'],
    ].map(([name, route, desc]) => `
    <div class="ol-card">
    <div class="ol-h2">${name}</div>
    <p class="ol-p">${desc}</p>
    <button class="ol-tab-button" onclick="OL_GOTO('${route}')">Open</button>
    </div>
    `).join('')}
    </div>
    `,
    onMount() {}
};
