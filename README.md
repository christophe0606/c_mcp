# README

MCP parser library that provides MCP protocol on serial port.

The C library supports serial transport. The Python host bridge below exposes
that serial connection as a shared HTTP MCP endpoint.

Focus is development, debug and tests.
This library is not intended for use in a final product.

With MCP on your board, you easily can add any automation : the board can be
controlled by scripts running on your computer or by AI agents.

The automation is at application level and thus complement what can be done
with a debugger.


## Serial-to-HTTP MCP bridge

Serial port has to be shared with all the scripts and agent that may want to
communicate with the board.

A ptyhon bridge si provided for this purpose:

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
Older tools-only firmware remains supported. Recovery depends on what happens
to the USB-to-UART connection:

* If a board reboot or power cycle disconnects the serial device, the bridge
  automatically reconnects, rediscovers both catalogs and notifies clients
  when their definitions changed. No bridge restart is needed.
* An MCU-only reset (for example, a debugger reset after flashing) can leave
  the separately powered USB-to-UART adapter connected and the host serial
  handle open. If UART requests continue to succeed, the bridge cannot detect
  that reset and keeps its cached catalogs. Restart the bridge if the new
  firmware changed the tools, resources or their schemas.
* If a UART request fails or times out during that MCU reset, automatic
  reconnection and rediscovery also occur. Uncertain requests are never replayed.

The distinction is whether the serial connection actually disconnects or fails,
not just whether the reset is called a board reboot or an MCU reset.

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

The portable core consists of `mcp.c`, `cJSON.c` and `serial_transport.c`, with their headers.
It uses standard C and has no socket, pthread, atomic or POSIX dependency.
Applications register tools with `add_tool()`, `set_tool_callback()` and
`add_argument()`; the core now provides `handle_tools_call()`.Arguments arrive in registration order
(older prepend/reverse-registration behavior is replaced). `free_tools()` releases
the registry. Use `dispatch_with_sender()` to invoke callbacks; direct calls to
`handle_tools_call()` are internal to an active request.
The public MCP header supports both C and C++ callers.

`dispatch(line, fd)` sends newline-delimited JSON through the
`mcp_serial_send()` hook (stdout by default). `dispatch_with_sender(line, fd, callback)` instead
calls `callback(json, fd)` synchronously. The JSON pointer is borrowed for
the duration of the callback. A NULL JSON pointer denotes a notification:
the default serial sender emits nothing. A NULL callback selects
`mcp_serial_send()`.

Both dispatchers reject malformed envelopes and trailing input, ignore
notifications without invoking tools, and use null IDs for invalid requests.
Integer tool arguments advertise the JSON Schema `integer` type.

The portable serial input loop, using stdio by default, is enabled by default:

```sh
cmake -S . -B build
cmake --build build
```

For a portable library with an application-owned input loop (or compile the
three core sources directly with `C_MCP_ENABLE_SERIAL_LOOP=0`):

```sh
cmake -S . -B build-core -DCMCP_BUILD_SERIAL=OFF
cmake --build build-core
```

`CMCP_BUILD_SERIAL` controls the optional input loop, not the I/O functions.
Embedded applications can use that loop or supply their own line parser, and
may set `CJSON_NESTING_LIMIT` to bound JSON recursion.
Test fixtures, test sources and test target definitions live in `tests/`.
The library supplies no application `main()`; applications register their own
tools and run their input loop.

Run the portable core and serial tests (including a VFS-disabled executable)
from this repository. CTest also runs the default stdio fixture through Python;
Python 3 is required only when building tests. The separate bridge tests exercise
the host Python HTTP endpoint without hardware:

```sh
cmake -S . -B build-core -DCMCP_BUILD_SERIAL=OFF -DCMCP_BUILD_TESTS=ON
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
uv run --script tests/test_serial_bridge.py -v
```

## Override the serial interface

`serial_transport.h` documents four I/O functions: `mcp_serial_transport_init()`,
`mcp_serial_transport_close()`, `mcp_serial_getchar()` and `mcp_serial_send()`.
Their defaults initialize unbuffered stdout, leave stdin/stdout open, read from
stdin and synchronously write JSON plus LF to stdout. On boards,
`serial_transport.c` includes the available `cmsis_compiler.h` and marks its
defaults with CMSIS `__WEAK`. Define strong functions with the same signatures
in a board application source file to replace any hooks. The public declarations
are not weak, so overrides must not carry a weak attribute. Linux, macOS and
Windows builds use ordinary stdio definitions without weak symbols or linker
aliases. The UART override example below is for CMSIS boards.

```c
#include "serial_transport.h"
/* Implement these board-specific operations in the application. */
extern int uart_try_get_byte(void); /* Byte or -1 when RX is empty. */
extern void uart_write_and_wait(const char *text);

int mcp_serial_getchar(void) {
    int ch = uart_try_get_byte();
    return ch < 0 ? MCP_SERIAL_NO_DATA : ch;
}
void mcp_serial_send(const char *json, int channel) {
    (void)channel;
    if (json) {
        uart_write_and_wait(json);
        uart_write_and_wait("\n");
    }
}
```

Read hooks distinguish temporary empty RX (`MCP_SERIAL_NO_DATA`) from closure
(`MCP_SERIAL_EOF`) and lost input (`MCP_SERIAL_INPUT_LOST`). Writes must finish
before the sender returns, because its JSON buffer is borrowed request storage.
An explicit `dispatch_with_sender()` callback takes precedence over the default sender.

For the optional input loop, call `init_serial()`, then `process_serial()` from the
main loop, and `end_serial()` at shutdown. `process_serial()` preserves partial lines,
accepts CR/LF/CRLF, and discards oversized or corrupted input through the next
delimiter. Its buffer is configured by `C_MCP_SERIAL_LINE_SIZE` (default 4096,
including the terminating null). The loop uses no request heap or POSIX APIs.
The application handles the returned EOF status.

