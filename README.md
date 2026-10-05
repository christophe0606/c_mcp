# README

MCP server (generated a lot with chat GPT help)
Can provide MCP on stdio and on http.

The `tools.cpp` is not built into the library since it must be provided by the application using the MCP server.

`tools.cpp` is just used to build the demo example.

## Serial-to-HTTP MCP bridge

`tools/mcp_serial_bridge.py` exposes a board's newline-delimited UART MCP
server at `http://127.0.0.1:8765/mcp`. Requires Python 3.10 or newer and `uv`.
From this repository's root, start one shared bridge (adjust the serial port):

```sh
uv run --script tools/mcp_serial_bridge.py --port COM5
```

UV installs the inline dependencies into a cached script environment.
For an existing application environment or editor analysis, install
`tools/requirements-mcp-serial-bridge.txt` into that environment:

```sh
uv pip install --python <venv-python> -r tools/requirements-mcp-serial-bridge.txt
```

Replace `<venv-python>` with the environment's Python executable. When this
repository is a submodule, prefix the script and requirements paths with its
location in the parent repository. Keep both dependency declarations in sync.
The bridge must be the serial port's only owner; Ctrl+C stops it and releases
the port. Use `--smoke-test` to check an already running bridge through HTTP.

## Portable core and optional transports

### Optional read-only resources

`C_MCP_ENABLE_VFS` in `c_mcp_config.h` defaults to 1. Set it to 0 (or configure
CMake with `-DCMCP_ENABLE_VFS=OFF`) to omit resource storage, response templates,
handlers and the advertised resource capability. Tools continue to work.

Register exact resource URIs during startup, before `mcp_prepare()`:

```c
static int read_temperature(const char **message)
{
    *message = mcp_arena_strdup("temperature: 24 C");
    return *message ? 0 : MCP_INTERNAL_ERROR;
}

/* Check registration and preparation results in application startup code. */
add_resource("sensor://board/temperature", "Temperature",
             "Latest board temperature", "text/plain", read_temperature);
```

The callback receives no arguments and returns static or request-arena text.
Zero means success; negative status produces a JSON-RPC error, preserving
protocol error codes and mapping other failures to `MCP_INTERNAL_ERROR`.
Resource registration strings must remain valid until `free_tools()`, which
releases both registries. Registration returns NULL for disabled builds,
duplicates, invalid definitions, late registration or allocation failure.

`resources/list` returns cached metadata; `resources/read` resolves the URI in
a separate AVL index and invokes its callback. Missing URIs return
`MCP_RESOURCE_NOT_FOUND` (-32002). This API exposes virtual text only; it does
not access the host filesystem, support writes, subscriptions or URI templates.
Core discovery is not paginated. Resource requests use the same bounded arena
and synchronous response lifetime as tool calls.

The serial bridge discovers resources when firmware advertises them, forwards
reads and preserves resource error codes. Reconnection refreshes both catalogs
and sends standard list-change notifications when their definitions change.
Older tools-only firmware remains supported. If a firmware reboot leaves the
serial connection healthy, restart the bridge to refresh its cached catalogs.

### Bounded request memory

Callbacks use `int tool(int argc, const char **returnMessage, const char **args)`.
The AVL lookup selects the stored function pointer directly. The core validates
required arguments, types, finite numbers and integral integers before invoking
the callback; unknown and duplicate input keys are rejected. Strings are decoded,
numbers use JSON-compatible text, and booleans are `true`/`false`. Callbacks own
range/enum validation. The output pointer must reference static or arena memory;
use `mcp_arena_alloc()` / `mcp_arena_strdup()` for varying text. Zero means success;
JSON-RPC error codes produce protocol errors, other negative values produce an
MCP result with `isError: true`. Missing output and positive statuses are errors.

`set_boolean_argument_alias()` supports an older boolean input for a canonical
string argument, mapping true/false to registered static strings. The canonical
schema stays unchanged, and clients cannot supply both names.

Tool names are indexed by an intrusive AVL tree, with one node in each tool
allocation. Sorted, reverse and arbitrary registration orders remain balanced.
`find_tool()` takes O(log M) string comparisons for M tools (each comparison is
O(L) in the name length). Duplicate or empty names are rejected. Names and
descriptions passed during registration must remain valid until `free_tools()`.
The linked list remains only for discovery enumeration. Index height and lookup
comparison counts are available for diagnostics.

Register tools during startup, then call `mcp_prepare()` and check its return
value. It caches the tool schemas and allocates reusable success, error and text
response templates. Registration is closed until `free_tools()` starts a new
startup phase. Dispatch lazily prepares for older applications.

`c_mcp_config.h` provides a Configuration Wizard setting for
`C_MCP_ARENA_SIZE` (default 32 KiB). Request parsing, callback output, changing
response text and serialization use this aligned arena, with no request-time
heap allocation. `mcp_arena_alloc()` and `mcp_arena_strdup()` are available only
during dispatch. The arena resets after the synchronous sender returns, so it
must not retain any pointer. Exhaustion returns an internal error from independent
bounded storage, and the next request can proceed. `mcp_arena_used()`,
`mcp_arena_high_water()` and `mcp_heap_allocations()` expose diagnostic counters.

Dispatch is single-threaded and non-reentrant. The cJSON hooks are global: do not
replace them or use cJSON concurrently during dispatch. Outside dispatch, cJSON
uses the heap normally; response helpers return owned objects. Inside dispatch,
the response helpers return borrowed reusable templates; do not delete or retain
them. Complete all transport writes before returning from the sender.

The portable core consists of `mcp.c` and `cJSON.c`, with their headers.
It uses standard C and has no socket, pthread, atomic or POSIX dependency.
Applications register tools with `add_tool()`, `set_tool_callback()` and
`add_argument()`; the core now provides `handle_tools_call()`. Remove the old
application dispatcher when migrating. Arguments arrive in registration order
(older prepend/reverse-registration behavior is replaced). `free_tools()` releases
the registry. Use `dispatch_with_sender()` to invoke callbacks; direct calls to
`handle_tools_call()` are internal to an active request.
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

Run portable core tests (including a separate VFS-disabled executable) and
hardware-free HTTP bridge integration tests from this repository:

```sh
cmake -S . -B build-core -DCMCP_BUILD_HTTP=OFF -DCMCP_BUILD_STDIO=OFF -DCMCP_BUILD_TESTS=ON
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
uv run --script tests/test_serial_bridge.py -v
```

