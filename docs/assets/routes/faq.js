window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['faq'] = {
  template: `
  <div class="ol-h1">FAQ</div>

  <div class="ol-collapse">
    <div class="ol-collapse-header">
      <div>How do I avoid blocking the event loop?</div><div>▾</div>
    </div>
    <div class="ol-collapse-body">
      <p class="ol-p">
        Use <code>ol_async_run_on_loop</code> for loop-bound callbacks (return a value or resolve via the promise).
        For waiting on results from the loop thread, use <code>ol_await_future_with_loop</code> to poll cooperatively.
      </p>
    </div>
  </div>

  <div class="ol-collapse" style="margin-top:8px;">
    <div class="ol-collapse-header">
      <div>What’s the difference between observables and streams?</div><div>▾</div>
    </div>
    <div class="ol-collapse-body">
      <p class="ol-p">
        They’re sibling abstractions focused on backpressure and operator composition. The APIs differ slightly,
        but the design principles (buffered queues, demand, operators, timer/IO sources) are aligned.
      </p>
    </div>
  </div>

  <div class="ol-collapse" style="margin-top:8px;">
    <div class="ol-collapse-header">
      <div>Can I run multiple event loops?</div><div>▾</div>
    </div>
    <div class="ol-collapse-body">
      <p class="ol-p">
        Yes, each loop is single-threaded by design. Use separate threads to drive them. Coordinate via channels,
        promises, and wake calls.
      </p>
    </div>
  </div>
  `,
  onMount() {}
};
