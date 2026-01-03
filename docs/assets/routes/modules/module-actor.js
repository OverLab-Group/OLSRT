window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-actor'] = {
    template: `
    <div class="ol-h1">Actor model</div>
    <p class="ol-p">
    Lightweight actors with a mailbox, behavior dispatch, and ask/reply semantics. Running on a parallel pool,
    actor loops process messages and safely handle ownership and promise resolution.
    </p>

    <div class="ol-h2">Core concepts</div>
    <ul>
    <li><b>Mailbox</b> (<code>ol_channel_t</code>): incoming messages and ask envelopes.</li>
    <li><b>Behavior</b> (callback): inspects messages, consumes ownership, and may request stop.</li>
    <li><b>Ask/reply</b>: envelopes contain a promise; behaviors must resolve via reply helpers.</li>
    <li><b>Ownership</b>: channel destructor is authoritative; behaviors set <code>msg = NULL</code> when consumed.</li>
    </ul>

    <div class="ol-h2">API summary</div>
    <div class="ol-card">
    <ul>
    <li><code>ol_actor_create(pool, capacity, dtor, initial, user_ctx)</code></li>
    <li><code>ol_actor_start / stop / close / destroy</code></li>
    <li><code>ol_actor_send / try_send / send_deadline</code></li>
    <li><code>ol_actor_ask(payload)</code> → <code>ol_future_t*</code></li>
    <li><code>ol_actor_become(next)</code>, <code>ol_actor_ctx</code>, <code>ol_actor_is_running</code></li>
    </ul>
    </div>

    <div class="ol-h2">Example: Request/Response behavior</div>
    <div class="ol-code">
    <button class="ol-copy-btn">Copy</button>
    <pre>
    // Simplified echo behavior using ask envelope
    int echo_beh(ol_actor_t* a, void* msg) {
        ol_ask_envelope_t* env = (ol_ask_envelope_t*)msg;
        if (!env || !env->reply) return 0;
        // reply OK with original payload; free in reply
        ol_actor_reply_ok(env, env->payload, free);
        msg = NULL;
        return 0;
    }

    ol_actor_t* act = ol_actor_create(pool, 64, free, echo_beh, NULL);
    ol_actor_start(act);

    // ask something
    char* payload = strdup("ping");
    ol_future_t* f = ol_actor_ask(act, payload);
    if (ol_future_await(f, 0) == 1) {
        const char* res = (const char*)ol_future_get_value_const(f);
        // res == "ping"
    }
    ol_future_destroy(f);
    ol_actor_stop(act);
    ol_actor_destroy(act);
    </pre>
    </div>

    <div class="ol-h2">Best practices</div>
    <ul>
    <li>Always free or transfer ownership explicitly. For ignored ask envelopes, the loop cancels and frees defensively.</li>
    <li>For graceful stop, return <code>br > 0</code> in behaviors; loop flips the running flag.</li>
    <li>Use channel capacities for backpressure if needed.</li>
    </ul>
    `,
    onMount() {}
};
