# gold_ws — v1.3.2 patch notes

This patch fixes the "**File not found**" message returned by
PHP-FPM when browsing `http://localhost:8080/` or
`http://localhost:8080/index.php`.

## Root causes

1. **Relative `SCRIPT_FILENAME`.** `script_root` defaulted to the
string `"public"`. PHP-FPM runs as its own process with its own
working directory (usually `/`), so a `SCRIPT_FILENAME` of
`public/index.php` was looked up as `/public/index.php` and
never found. Every dynamic request came back with PHP-FPM's
`File not found` body.
2. **Directory as filename.** For `GET /`, `serve_php` built
`script_path = script_root + "/"` = `public/` — a directory, not
a file. PHP-FPM refuses to execute a directory and answers
`File not found`.
3. **Query string contamination.** The path was concatenated into
`script_path` before the `?` was stripped, so
`GET /index.php?foo=bar` produced
`SCRIPT_FILENAME = public/index.php?foo=bar`. PHP-FPM found
nothing.

## What changed

### `main.c`

- Added `make_abs_path()` — resolves a path with `realpath()` when
it exists, and falls back to `getcwd() + "/" + rel` otherwise.
- `root` and `script_root` are now absolute before being handed to
the router. This is what makes PHP-FPM able to find the scripts.
- A warning is printed at startup if `script_root` does not exist,
so a misconfigured CWD is caught immediately instead of showing
up as an opaque `File not found` from FPM.

### `router.c`

- `serve_php()` now strips the query string **before** building
`script_path`.
- `GET /` is rewritten to `/index.php` for the FPM call.
- Docstring updated to explain the reasoning.

## How to verify

```
cd demos/gold_ws
make clean && make
./gold_ws
```

Watch the startup banner. It should print something like:

```
script root  : /home/you/olsrt/demos/gold_ws/public
```

If it prints a relative path, `realpath()` and `getcwd()` both
failed — that is the only remaining reason for `File not found`.

Then, from another terminal:

```
curl -i http://localhost:8080/
curl -i http://localhost:8080/index.php
curl -i http://localhost:8080/index.php?foo=bar
curl -i http://localhost:8080/style.css
```

All four should return real content, not `File not found`.

## If it still fails

Check the PHP-FPM pool configuration
(`/etc/php/*/fpm/pool.d/www.conf`):

- `security.limit_extensions` must include `.php` (it does by
default).
- The user PHP-FPM runs as (usually `www-data`) must have read
permission on `public/index.php` and execute permission on every
parent directory.
- `catch_workers_output = yes` in the pool config makes
PHP-FPM log its actual error to `/var/log/php*-fpm.log`, which
tells you exactly which path it tried to open.

To check what FPM sees for a given request, run:

```
sudo tail -f /var/log/php*-fpm.log
```

while hitting `curl -i http://localhost:8080/index.php`.