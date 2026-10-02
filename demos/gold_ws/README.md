# gold_ws — Gold Web Server

A working HTTP/1.1 web server built entirely on OLSRT primitives.

> **Gold** demos are the ones that take OLSRT past a toy. `gold_ws`
> is the first: a real server that accepts connections, parses
> requests, spawns an actor per request, serves static files, and
> proxies PHP to a PHP-FPM pool.

## What it does

- Listens on `0.0.0.0:8080`.
- Accepts TCP connections with `ol_tcp_socket_accept`.
- For **every HTTP request** (not every connection) it spawns a
  dedicated actor via `ol_actor_create` + `ol_actor_ask`.
- The actor parses the request using a small hand-written HTTP/1.1
  parser, chooses a handler, and produces a response.
- `.php` requests are forwarded to PHP-FPM on `localhost:9000` over
  the FastCGI protocol; the response is parsed and returned to the
  client.
- Static files under `public/` are served directly.
- The connection is closed after the response (`Connection: close`).

The intention is not to compete with nginx. It is to show that
OLSRT can host a non-trivial server workload end to end, without
external dependencies.

## Layout

```
gold_ws/
├── main.c          entry point, listener, accept loop
├── http.h / http.c minimal HTTP/1.1 parser and response builder
├── router.h / router.c URL routing and handler dispatch
├── static.h / static.c static file serving from public/
├── fpm.h / fpm.c   FastCGI client for PHP-FPM
├── public/
│   ├── index.php   landing page
│   ├── style.css
│   └── favicon.ico (optional)
├── Makefile
└── README.md
```

## Build

```
cd demos/gold_ws
make
```

Requires OLSRT to be built first:

```
cd ../..
make linux
```

The `Makefile` links against `../../bin/linux/x86_64/libolsrt.so`.

## Run

Terminal 1 — start PHP-FPM:

```
# On Debian / Ubuntu:

sudo apt install php-fpm
sudo service php8.2-fpm start

# The pool listens on 127.0.0.1:9000 by default.

# Verify with:

ss -tlnp | grep :9000
```

Terminal 2 — start gold_ws:

```
cd demos/gold_ws
./gold_ws
```

Terminal 3 — try it:

```
curl -i http://localhost:8080/
curl -i http://localhost:8080/style.css
curl -i http://localhost:8080/index.php
curl -i http://localhost:8080/missing
```

## Configuration

The server reads a handful of environment variables at startup:

| Variable | Default | Meaning |
|----------|---------|---------|
| `GOLD_WS_PORT` | `8080` | Listen port |
| `GOLD_WS_ROOT` | `public` | Static file directory |
| `GOLD_WS_FPM_HOST` | `127.0.0.1` | PHP-FPM host |
| `GOLD_WS_FPM_PORT` | `9000` | PHP-FPM port |
| `GOLD_WS_WORKERS` | `4` | Parallel pool size for actor driver threads |

## Architecture

```
+---------------+
|   listener    |  (main thread)
+-------+-------+
|
| ol_tcp_socket_accept  →  future<client>
|
v
+-------------------------------+
|  accept loop (main thread)    |
|  forwards client fd to        |
|  parallel pool                |
+---------------+---------------+
|
v
+-------------------------------+
|  worker thread                |
|  reads request, creates an    |
|  actor via ol_actor_create,   |
|  sends an ask envelope with   |
|  the request bytes            |
+---------------+---------------+
|
v
+-------------------------------+
|  per-request actor            |
|  - parses the request         |
|  - dispatches on URL          |
|  - static  →  file read       |
|  - .php    →  FastCGI call    |
|  - 404     →  empty response  |
|  - replies via promise        |
+---------------+---------------+
|
v
+-------------------------------+
|  worker sends response on     |
|  the client fd, closes        |
+-------------------------------+
```

Every request gets its own actor. The actor's mailbox, private
arena and promise reply live only for the lifetime of that request,
then are freed. This is a clean fit for the OLSRT actor model: no
long-lived per-connection state.

## Limitations

- **HTTP/1.1 only.** No pipelining, no chunked transfer, no HTTP/2.
- **`Connection: close`** on every response. Keep-alive is on the
  v1.4 roadmap once the dataflow worker drains edge inboxes.
- **Single-threaded FastCGI.** The PHP-FPM connection is opened per
  request. A pooled client lands when ORoutines ship in v1.3.4.
- **No TLS.** Termination is intended to be done by a front proxy.

## License

Apache-2.0. See the repository root `LICENSE`.
