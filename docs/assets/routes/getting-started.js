window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['getting-started'] = {
    template: `
    <div class="ol-h1">Getting started</div>
    <p class="ol-p">
    Below is a staged walkthrough—from initializing a thread pool and event loop, to creating channels,
    sending/receiving items, scheduling timers, and wiring a simple actor with ask/reply semantics.
    </p>

    <div class="ol-tabs" style="margin-top:16px;">
    <div class="ol-tab-buttons">
    <button class="ol-tab-button">Setup</button>
    <button class="ol-tab-button">Messaging</button>
    <button class="ol-tab-button">Async & Loop</button>
    <button class="ol-tab-button">Actor ask/reply</button>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-h2">Initialize core subsystems</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Pseudocode mixing APIs from modules
    // Create a parallel pool with 4 worker threads
    ol_parallel_pool_t *pool = ol_parallel_create(4);

    // Event loop (single thread)
    ol_event_loop_t *loop = ol_event_loop_create();

    // Deadlines utility
    ol_deadline_t dl = ol_deadline_from_ms(500);

    // Always check for NULL on creation and clean up on errors.
    </pre>
    </div>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-h2">Use channels for messaging</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Create a channel (unbounded capacity) with optional destructor
    ol_channel_t *ch = ol_channel_create(0, free);

    // Send items (ownership transferred)
    char *msg = strdup("hello");
    int s = ol_channel_send(ch, msg); // 0 on success

    // Receive
    void *out = NULL;
    int r = ol_channel_recv(ch, &out); // 1 item, 0 closed+empty, -3 timeout
    if (r == 1) {
        // Use message, then free if you own it
        printf("%s\n", (char*)out);
        free(out);
    }

    // Close and destroy
    ol_channel_close(ch);
    ol_channel_destroy(ch);
    </pre>
    </div>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-h2">Schedule work on the event loop</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Register a one-shot timer that fires soon
    ol_deadline_t soon = ol_deadline_from_ns(1);
    uint64_t tid = ol_event_loop_register_timer(loop, soon, 0,
                                                /* cb: */ [](ol_event_loop_t* lp, ol_ev_type_t type, int fd, void* ud) {
                                                    (void)type; (void)fd; (void)ud;
                                                    printf("Timer fired!\\n");
                                                }, NULL);

    // Run in a blocking mode (single-thread loop)
    ol_event_loop_run(loop);

    // From another thread, you can wake the loop:
    ol_event_loop_wake(loop);

    // Stop and destroy
    ol_event_loop_stop(loop);
    ol_event_loop_destroy(loop);
    </pre>
    </div>
    </div>

    <div class="ol-tab-panel">
    <div class="ol-h2">Actor ask / reply pattern</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Behavior signature: int beh(ol_actor_t* a, void* msg);
    // 'Ask' envelopes carry 'payload' and a 'reply' promise handle.

    // Sample behavior that replies with uppercase payload:
    int uppercase_beh(ol_actor_t* a, void* msg) {
        ol_ask_envelope_t* env = (ol_ask_envelope_t*)msg;
        if (!env || !env->reply) return 0;

        char* in = (char*)env->payload;
        if (!in) { /* error */ ol_actor_reply_err(env, -1); return 0; }

        for(char *p = in; *p; ++p) *p = (char)toupper(*p);

        // fulfill with transformed value; provide destructor
        ol_actor_reply_ok(env, in, free);
        // set msg to NULL to indicate consumption
        msg = NULL;
        return 0;
    }

    // Create actor
    ol_actor_t* act = ol_actor_create(pool, /*capacity*/128, /*dtor*/free, uppercase_beh, NULL);
    ol_actor_start(act);

    // Ask
    char* payload = strdup("overlab");
    ol_future_t* fut = ol_actor_ask(act, payload);

    // Await (blocking or with deadlines)
    int ar = ol_future_await(fut, /*deadline*/ 0);
    if (ar == 1) {
        const char* res = (const char*)ol_future_get_value_const(fut);
        printf("Reply: %s\\n", res); // "OVERLAB"
    }

    // Cleanup
    ol_future_destroy(fut);
    ol_actor_stop(act);
    ol_actor_destroy(act);
    </pre>
    </div>
    </div>
    </div>
    `,
    onMount() {}
};
