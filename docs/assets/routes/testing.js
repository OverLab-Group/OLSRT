window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['testing'] = {
    template: `
    <div class="ol-h1">Testing & Tooling</div>
    <p class="ol-p">A practical checklist for correctness, safety, and performance.</p>

    <div class="ol-h2">Unit tests</div>
    <ul>
    <li><b>Channels:</b> bounded/unbounded semantics, deadline timeouts, destructor invocation on closed sends.</li>
    <li><b>Promises/Futures:</b> fulfill/reject/cancel paths, continuations, take_value ownership.</li>
    <li><b>Actors:</b> send/try_send/send_deadline, ask/reply lifecycle, behavior swapping, mailbox draining.</li>
    <li><b>Event loop:</b> timer one-shot/periodic, wake pipe, IO registration/unregistration.</li>
    <li><b>Reactive/Streams:</b> backpressure (demand), buffer draining, operator correctness.</li>
    <li><b>Supervisor:</b> strategies across failure modes, intensity window resets.</li>
    </ul>

    <div class="ol-h2">Sanitizers</div>
    <ul>
    <li>ASan/Valgrind for leaks and buffer overruns.</li>
    <li>TSan for race detection when crossing threads.</li>
    <li>UBSan for undefined behavior in corner cases.</li>
    </ul>

    <div class="ol-h2">Fuzzing</div>
    <p class="ol-p">
    AFL++ or libFuzzer against core APIs (channel sends with randomized close/deadline races; async task wrappers).
    Ensure destructor paths are robust when scheduling fails or is rejected.
    </p>

    <div class="ol-h2">Benchmarks</div>
    <ul>
    <li>Compare direct call vs scheduled call (async run/loop) overhead.</li>
    <li>Channel throughput under contention (multiple producers/consumers).</li>
    <li>Event loop timer dispatch latency with epoll/kqueue/select backends.</li>
    </ul>

    <div class="ol-h2">Static analysis</div>
    <p class="ol-p">
    Document ownership and thread-safety assumptions inline (which you’ve done). Run clang-tidy/cppcheck frequently.
    </p>
    `,
    onMount() {}
};
