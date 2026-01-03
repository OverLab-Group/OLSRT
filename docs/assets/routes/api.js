window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['api'] = {
    template: `
    <div class="ol-h1">API Reference</div>
    <p class="ol-p">Extracted highlights across modules (select the module pages for deeper examples).</p>

    <div class="ol-grid cols-2" style="margin-top:10px;">
    <div class="ol-card">
    <div class="ol-h2">Channels</div>
    <ul>
    <li><code>ol_channel_create(capacity, dtor)</code></li>
    <li><code>ol_channel_send / recv / try_send / try_recv / send_deadline / recv_deadline</code></li>
    <li><code>ol_channel_close / destroy</code></li>
    <li><code>ol_channel_len / capacity / is_closed</code></li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Event loop</div>
    <ul>
    <li><code>ol_event_loop_create / destroy / run / stop / wake</code></li>
    <li><code>ol_event_loop_register_io / mod_io / unregister</code></li>
    <li><code>ol_event_loop_register_timer(deadline, periodic_ns, cb, ud)</code></li>
    <li>Tags identify registered callbacks.</li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Promises & Futures</div>
    <ul>
    <li><code>ol_promise_create(loop?)</code>, <code>get_future</code>, <code>destroy</code></li>
    <li><code>fulfill(value, dtor)</code>, <code>reject(code)</code>, <code>cancel()</code></li>
    <li><code>ol_future_await(deadline_ns)</code> returns 1/-3/-1</li>
    <li><code>ol_future_then(cb)</code>, <code>get_value_const</code>, <code>take_value</code></li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Actors</div>
    <ul>
    <li><code>ol_actor_create(pool, capacity, dtor, initial, user_ctx)</code></li>
    <li><code>ol_actor_start / stop / close / destroy</code></li>
    <li><code>ol_actor_send / try_send / send_deadline</code></li>
    <li><code>ol_actor_ask(payload)</code> returns future</li>
    <li><code>ol_actor_become(next)</code>, <code>ol_actor_ctx</code>, <code>is_running</code></li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Async</div>
    <ul>
    <li><code>ol_async_run(pool, task_fn, arg, dtor)</code> → future</li>
    <li><code>ol_async_run_on_loop(loop, cb, arg, dtor)</code> → future</li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Parallel pool</div>
    <ul>
    <li><code>ol_parallel_create(num_threads)</code></li>
    <li><code>ol_parallel_submit(pool, fn, arg)</code></li>
    <li><code>ol_parallel_flush / shutdown / destroy</code></li>
    <li><code>ol_parallel_thread_count / queue_size / is_running</code></li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Reactive & Streams</div>
    <ul>
    <li>Observables: <code>subject_create</code>, <code>subscribe</code>, <code>on_next</code>, <code>on_error</code>, <code>on_complete</code></li>
    <li>Operators: <code>map</code>, <code>filter</code>, <code>take</code>, <code>debounce</code>, <code>merge</code></li>
    <li>Sources: <code>timer</code>, <code>from_fd</code></li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Supervisor</div>
    <ul>
    <li><code>ol_supervisor_create(strategy, max_restarts, window_ms)</code></li>
    <li><code>start / stop / destroy</code></li>
    <li><code>add_child / remove_child / restart_child</code></li>
    <li><code>is_running</code>, child count</li>
    </ul>
    </div>

    <div class="ol-card">
    <div class="ol-h2">Dataflow</div>
    <ul>
    <li><code>graph_create / destroy / start / stop</code></li>
    <li><code>node_create / node_remove</code></li>
    <li><code>connect / disconnect</code></li>
    <li><code>push / emit</code></li>
    </ul>
    </div>
    </div>
    `,
    onMount() {}
};
