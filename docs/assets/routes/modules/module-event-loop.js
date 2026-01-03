window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-event-loop'] = {
    template: `
    <div class="ol-h1">Event loop</div>
    <p class="ol-p">
    Portable loop with IO and timer support, plus a wake pipe to interrupt blocking waits.
    Uses a poller backend (epoll/kqueue/select) abstracted by tags.
    </p>

    <div class="ol-h2">Core features</div>
    <ul>
    <li>Register/unregister IO and timers with unique tags.</li>
    <li>Wake from other threads via the write-end of the pipe.</li>
    <li>Timers use monotonic deadlines; periodic scheduling supported.</li>
    </ul>

    <div class="ol-h2">Example: IO + timer</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // IO registration
    uint64_t id = ol_event_loop_register_io(loop, fd, OL_POLL_IN,
                                            [](ol_event_loop_t* lp, ol_ev_type_t type, int fd, void* ud){
                                                (void)lp; (void)type; (void)ud;
                                                // read from fd
                                            }, NULL);

    // Timer registration
    ol_deadline_t dl = ol_deadline_from_ms(250);
    uint64_t tid = ol_event_loop_register_timer(loop, dl, /*periodic*/ 0,
                                                [](ol_event_loop_t* lp, ol_ev_type_t type, int fd, void* ud){
                                                    (void)lp; (void)type; (void)fd; (void)ud;
                                                    // do periodic work
                                                }, NULL);

    ol_event_loop_run(loop);
    </pre>
    </div>

    <div class="ol-h2">Notes</div>
    <ul>
    <li>Wake pipe uses tag 0 (reserved); be sure not to collide with user tags.</li>
    <li>Poller wait clamps long timeouts; overflow-safe.</li>
    </ul>
    `,
    onMount() {}
};
