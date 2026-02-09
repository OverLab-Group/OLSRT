window.OL_ROUTES = window.OL_ROUTES || {};
window.OL_ROUTES['module-green-threads'] = {
    template: `
    <div class="ol-h1">Green threads</div>
    <p class="ol-p">
    Platform backends for coroutines: Windows Fibers and POSIX ucontext. Handles scheduling,
    cancellation flags, and safe destruction.
    </p>

    <div class="ol-h2">Key API (internal)</div>
    <ul>
    <li><code>ol_gt_scheduler_init / shutdown</code></li>
    <li><code>ol_gt_spawn(entry, arg, stack)</code>, <code>ol_gt_resume / yield / join / cancel / destroy</code></li>
    </ul>

    <div class="ol-h2">Considerations</div>
    <ul>
    <li>POSIX uses <code>getcontext/makecontext/swapcontext</code> with stacks.</li>
    <li>Windows uses Fibers (<code>CreateFiber/SwitchToFiber</code>).</li>
    <li>Join is cooperative; deadlocks are signaled by an error return if no yields occur.</li>
    </ul>
    `,
    onMount() {}
};
