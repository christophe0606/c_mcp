# README

MCP server (generated a lot with chat GPT help)
Can provide MCP on stdio and on http.

The `tools.cpp` is not built into the library since it must be provided by the application using the MCP server.

`tools.cpp` is just used to build the demo example.

## Portable core and optional transports

The portable core consists of `mcp.c` and `cJSON.c`, with their headers.
It uses standard C and has no socket, pthread, atomic or POSIX dependency.
Applications provide `handle_tools_call()` and register their tools using
`add_tool()` / `add_argument()`; `free_tools()` releases the registry.
The public MCP header supports both C and C++ callers.

`dispatch(line, fd)` writes newline-delimited JSON to stdout (suitable for
retargeted UART stdio). `dispatch_with_sender(line, fd, callback)` instead
calls `callback(json, fd)` synchronously. The JSON pointer is borrowed for
the duration of the callback. A NULL JSON pointer denotes a notification:
stdio emits nothing, while the HTTP transport returns HTTP 202. A NULL
callback selects stdout. Configure transport selection before using the
demo via `MCP_STDIO` as before; `process_http()` now selects its HTTP sender
explicitly. Direct HTTP users of `dispatch()` should migrate to
`dispatch_with_sender()` and supply their response sender.

Both dispatchers reject malformed envelopes and trailing input, ignore
notifications without invoking tools, and use null IDs for invalid requests.
Integer tool arguments advertise the JSON Schema `integer` type.

Linux and macOS builds retain both POSIX transports by default:

```sh
cmake -S . -B build
cmake --build build
```

For a portable core-only library (or compile the two core sources directly):

```sh
cmake -S . -B build-core -DCMCP_BUILD_HTTP=OFF -DCMCP_BUILD_STDIO=OFF
cmake --build build-core
```

`CMCP_BUILD_STDIO` controls only the POSIX `getline` input loop, not the
core's stdout response support. Embedded applications supply their own input
loop and may set `CJSON_NESTING_LIMIT` to bound JSON parser recursion.
`main.c`, `tools.c` and `processing.c` are demo/application files and are
never compiled into the library.

