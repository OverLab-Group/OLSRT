<?php
/**
 * gold_ws landing page.
 *
 * This file is served by gold_ws through PHP-FPM. It is a complete
 * landing page: hero, feature grid, live server info, and a footer.
 *
 * The style is intentionally simple so that the response fits into
 * the server's static response buffer. Real deployments can grow the
 * page; gold_ws allocates 512 KB per request.
 */

$started_at = $_SERVER['REQUEST_TIME_FLOAT'] ?? microtime(true);
$now        = microtime(true);
$server     = $_SERVER['SERVER_SOFTWARE'] ?? 'gold_ws';
$method     = $_SERVER['REQUEST_METHOD']  ?? 'GET';
$uri        = $_SERVER['REQUEST_URI']     ?? '/';
$protocol   = $_SERVER['SERVER_PROTOCOL'] ?? 'HTTP/1.1';
$client     = $_SERVER['REMOTE_ADDR']     ?? '127.0.0.1';

header('Content-Type: text/html; charset=utf-8');
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>gold_ws — OverLab HTTP/1.1 server</title>
<style>
:root {
    --bg:       #0f0b18;
    --bg-soft:  #1a1630;
    --border:   #3d2b63;
    --accent:   #7b49ff;
    --accent-2: #a682ff;
    --text:     #e8e2ff;
    --text-dim: #cbbfff;
}
* { box-sizing: border-box; }
html, body {
    margin: 0; padding: 0;
    background: var(--bg); color: var(--text);
    font-family: ui-sans-serif, system-ui, -apple-system, "Segoe UI",
                 Roboto, sans-serif;
    line-height: 1.6;
}
.wrap { max-width: 900px; margin: 0 auto; padding: 48px 24px; }
.hero {
    text-align: center;
    padding: 64px 24px;
    border: 1px solid var(--border);
    border-radius: 20px;
    background: linear-gradient(180deg,
                rgba(123,73,255,0.14), transparent);
}
.logo {
    display: inline-block;
    width: 72px; height: 72px; border-radius: 50%;
    background: radial-gradient(120% 120% at 0% 0%,
                #7b49ff 0%, #4723a6 60%, #1a1630 100%);
    color: white; font-weight: 700; font-size: 22px;
    line-height: 72px; text-align: center;
    margin-bottom: 20px;
    box-shadow: 0 8px 32px rgba(123,73,255,0.4);
}
h1 { font-size: 36px; margin: 0 0 12px; }
.tag { color: var(--text-dim); font-size: 18px; margin: 0; }
.grid {
    display: grid; gap: 16px; margin-top: 40px;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
}
.card {
    border: 1px solid var(--border); border-radius: 14px;
    padding: 20px;
    background: linear-gradient(180deg, rgba(123,73,255,0.08), transparent);
}
.card h3 { margin: 0 0 8px; font-size: 17px; color: var(--accent-2); }
.card p  { margin: 0; color: var(--text-dim); font-size: 14px; }
.info {
    margin-top: 40px; padding: 20px;
    border: 1px dashed var(--border); border-radius: 14px;
    font-family: ui-monospace, "SF Mono", Consolas, monospace;
    font-size: 13px;
}
.info table { width: 100%; border-collapse: collapse; }
.info td { padding: 6px 0; }
.info td:first-child { color: var(--text-dim); width: 42%; }
footer {
    margin-top: 40px; padding: 24px 0;
    border-top: 1px solid var(--border);
    text-align: center; color: var(--text-dim); font-size: 13px;
}
code {
    font-family: ui-monospace, "SF Mono", Consolas, monospace;
    background: var(--bg-soft); padding: 2px 6px; border-radius: 6px;
    color: var(--accent-2);
}
</style>
</head>
<body>
<div class="wrap">
    <div class="hero">
        <div class="logo">OL</div>
        <h1>gold_ws</h1>
        <p class="tag">A working HTTP/1.1 server built on OLSRT</p>
    </div>

    <div class="grid">
        <div class="card">
            <h3>Actor per request</h3>
            <p>Every HTTP request spawns a dedicated OLSRT actor,
               processes the request in isolation, and replies via
               a promise.</p>
        </div>
        <div class="card">
            <h3>TCP on OLSRT</h3>
            <p>Accept, recv, and send are all driven by OLSRT
               promises over the TCP socket layer.</p>
        </div>
        <div class="card">
            <h3>PHP-FPM integration</h3>
            <p>Dynamic requests are proxied to PHP-FPM over the
               FastCGI protocol. This page is one of them.</p>
        </div>
        <div class="card">
            <h3>Zero dependencies</h3>
            <p>gold_ws links only against libolsrt and libc. No
               third-party HTTP or FastCGI library is used.</p>
        </div>
    </div>

    <div class="info">
        <table>
            <tr><td>Server</td>   <td><?php echo htmlspecialchars($server); ?></td></tr>
            <tr><td>Method</td>   <td><?php echo htmlspecialchars($method); ?></td></tr>
            <tr><td>URI</td>      <td><?php echo htmlspecialchars($uri); ?></td></tr>
            <tr><td>Protocol</td> <td><?php echo htmlspecialchars($protocol); ?></td></tr>
            <tr><td>Client</td>   <td><?php echo htmlspecialchars($client); ?></td></tr>
            <tr><td>PHP version</td><td><?php echo PHP_VERSION; ?></td></tr>
            <tr><td>Render time</td>
                <td><?php printf('%.3f ms',
                        ($now - $started_at) * 1000.0); ?></td></tr>
            <tr><td>Timestamp</td>
                <td><?php echo htmlspecialchars(date('c')); ?></td></tr>
        </table>
    </div>

    <footer>
        <p>OverLab Streams Runtime &middot; v1.3.2 &middot;
           <code>gold_ws/1.0</code></p>
    </footer>
</div>
</body>
</html>
