# OLSRT - OverLab Streams Runtime

> Built in one language</br>
> But usable everywhere

## What is OLSRT?
**OLSRT** (**OverLab Streams Runtime**) is not just another runtime.
It started a humble attempt to bring Node.js, Golang, and Erlang (WebSocket) power to PHP...
but our old system, betrayed us, and we couldn't even get a proper build output.

**So we decided to go bigger**.
**OLSRT** is now a **universal runtime core** - designed for **all languages**.
If your language can talk to C, it can talk to **OLSRT**.

## Why **OLSRT** is the best?
The **OLSRT** core is written in such a way that no additional libraries or tools are needed.
Just you and the incredible capailities of **OLSRT**, which allows you to:
> Using Actors</br>
> Using Channels</br>
> Using Event Loop and Poller</br>
> Using HTTP</br>
> Using Parallel</br>
> Using Promise and Futures</br>
> Using Streams</br>
> Using Timers</br>
> Using WebSocket

Interesting enough to dive in?

## Status
We'll be honest:
-  No stable build yet.
-  No shiny binaries.
-  Just raw code, ideas, and a vision.

But that's why this repo exsists.
Because **your contributions mean everything**.
Even the smallest PR, a bugfix, or a test will make us *incredibly happy*.

## How to bind OLSRT to your language?
**OLSRT** exposes a clean C API.
Every modern language can connect via FFI:
-  **PHP** -> Use PHP SDK, write an extension (e.g `php_olsrt.c`), write `config.w32/m4`, compile it, then enjoy
-  **Python** -> `ctypes` / `cffi`
-  **Ruby** -> `ffi` gem
-  **C++** -> It can directly using `olsrt.h` and enjoy using it
-  **C# / .NET** -> `Dllimport` (P/Invoke)
-  **Node.js** -> `N-API` / `node-ffi`
-  **Golang** -> `cgo`
-  **Rust** -> `bindgen` + Safe wrappers

## Contributing
We are not a big team.
We are just trying, failing, and trying more, again.
If you join us, you won't just be a contributor.
You'll be part of the story.

## License
Apache 2.0 - free to use, modify, and share.

`By OverLab Group`
