(function () {
    const view = document.getElementById('ol-route-view');
    const navItems = document.querySelectorAll('.ol-nav-item, .ol-sidebar-link, .ol-footer a');
    const searchInput = document.getElementById('ol-search-input');
    const searchBtn = document.getElementById('ol-search-btn');
    const themeToggle = document.getElementById('ol-theme-toggle');

    const routes = window.OL_ROUTES || {};
    let currentRoute = 'home';

    function render(route) {
        const page = routes[route] || routes['home'];
        currentRoute = route;
        if (!page) return;

        // Smooth transition
        view.style.opacity = 0;
        setTimeout(() => {
            view.innerHTML = page.template;
            page.onMount && page.onMount();
            attachInteractions();
            view.style.opacity = 1;
        }, 160);
    }

    function attachInteractions() {
        // Tabs
        document.querySelectorAll('.ol-tabs').forEach(tabs => {
            const buttons = tabs.querySelectorAll('.ol-tab-button');
            const panels = tabs.querySelectorAll('.ol-tab-panel');
            buttons.forEach((btn, idx) => {
                btn.addEventListener('click', () => {
                    buttons.forEach(b => b.classList.remove('active'));
                    panels.forEach(p => p.classList.remove('active'));
                    btn.classList.add('active');
                    panels[idx].classList.add('active');
                });
            });
            // Activate first tab by default
            if (buttons[0]) buttons[0].click();
        });

            // Copy buttons
            document.querySelectorAll('.ol-code').forEach(code => {
                const btn = code.querySelector('.ol-copy-btn');
                const pre = code.querySelector('pre');
                if (!btn || !pre) return;
                btn.addEventListener('click', async () => {
                    try {
                        const raw = pre.innerText.replace(/\u00A0/g, ' ');
                        await navigator.clipboard.writeText(raw);
                        btn.textContent = 'Copied!';
                        setTimeout(() => (btn.textContent = 'Copy'), 1200);
                    } catch (e) {
                        btn.textContent = 'Copy failed';
                        setTimeout(() => (btn.textContent = 'Copy'), 1200);
                    }
                });
            });

            // Collapse
            document.querySelectorAll('.ol-collapse').forEach(c => {
                const header = c.querySelector('.ol-collapse-header');
                const body = c.querySelector('.ol-collapse-body');
                if (!header || !body) return;
                header.addEventListener('click', () => {
                    c.classList.toggle('open');
                    const open = c.classList.contains('open');
                    body.style.maxHeight = open ? body.scrollHeight + 'px' : '0px';
                });
                // initial
                body.style.maxHeight = '0px';
            });
    }

    // Theme toggle (light/dark-like variation within purple palette)
    themeToggle.addEventListener('click', () => {
        document.body.classList.toggle('ol-theme-light');
        const root = document.documentElement.style;
        if (document.body.classList.contains('ol-theme-light')) {
            root.setProperty('--ol-bg', '#f6f3ff');
            root.setProperty('--ol-bg-soft', '#f2efff');
            root.setProperty('--ol-bg-elev', '#e9e4ff');
            root.setProperty('--ol-text', '#1a1630');
            root.setProperty('--ol-text-soft', '#3d2b63');
            root.setProperty('--ol-border', '#c8bbff');
            root.setProperty('--ol-border-soft', '#d9d0ff');
            root.setProperty('--ol-shadow', '0 8px 28px rgba(123,73,255,0.24)');
        } else {
            root.removeProperty('--ol-bg');
            root.removeProperty('--ol-bg-soft');
            root.removeProperty('--ol-bg-elev');
            root.removeProperty('--ol-text');
            root.removeProperty('--ol-text-soft');
            root.removeProperty('--ol-border');
            root.removeProperty('--ol-border-soft');
            root.removeProperty('--ol-shadow');
        }
    });

    // Navigation
    navItems.forEach(item => {
        item.addEventListener('click', e => {
            e.preventDefault();
            const r = item.getAttribute('data-route');
            if (r) render(r);
        });
    });

    // Search: naive route search by keywords
    searchBtn.addEventListener('click', () => performSearch(searchInput.value));
    searchInput.addEventListener('keydown', e => {
        if (e.key === 'Enter') performSearch(searchInput.value);
    });

        function performSearch(q) {
            const query = (q || '').toLowerCase();
            if (!query.trim()) return;

            // Map keywords to routes
            const map = [
                ['actor', 'module-actor'],
                ['async', 'module-async'],
                ['await', 'module-await'],
                ['channel', 'module-channel'],
                ['event', 'module-event-loop'],
                ['poller', 'module-poller'],
                ['promise', 'module-promise'],
                ['future', 'module-promise'],
                ['coroutine', 'module-coroutines'],
                ['green', 'module-green-threads'],
                ['parallel', 'module-parallel'],
                ['deadline', 'module-deadlines'],
                ['reactive', 'module-reactive'],
                ['stream', 'module-streams'],
                ['semaphore', 'module-semaphores'],
                ['supervisor', 'module-supervisor'],
                ['dataflow', 'module-dataflow'],
                ['api', 'api'],
                ['test', 'testing'],
                ['guide', 'guides'],
                ['start', 'getting-started'],
            ];
            const match = map.find(([kw]) => query.includes(kw));
            render(match ? match[1] : 'modules');
        }

        // Expose route switch globally for deep-links in content
        window.OL_GOTO = r => render(r);

        // Initial render
        render('home');
})();
