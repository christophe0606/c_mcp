# CMSIS-MCP (c_mcp)

[![GitHub release (latest by date including pre-releases)](https://img.shields.io/github/v/release/christophe0606/c_mcp?include_prereleases)](https://github.com/christophe0606/c_mcp/releases/latest) 
[![GitHub](https://img.shields.io/github/license/christophe0606/c_mcp)](https://github.com/christophe0606/c_mcp/blob/main/LICENSE) 
[![C Tests](https://img.shields.io/github/actions/workflow/status/christophe0606/c_mcp/portable.yaml?logo=arm&logoColor=0091bd&label=Host%20Tests)](https://github.com/christophe0606/c_mcp/actions/workflows/portable.yaml)

CMSIS-MCP lets scripts and AI agents control an embedded application through
MCP messages. It is intended for development, debugging
and testing, rather than use in a final product. Application-level automation
complements the work done with a debugger.

The `ARM::CMSIS-MCP` pack provides the `CMSIS:MCP&Serial` firmware component.
A Python bridge shares the board's serial connection through an HTTP MCP
endpoint.

## Add the pack and host bridge layer

For an installed pack, add it to your existing csolution:

```yaml
solution:
  packs:
    - pack: ARM::CMSIS-MCP
```

For a local checkout or unpacked pack, use a versionless pack ID and the
path to the directory containing `ARM.CMSIS-MCP.pdsc` instead:

```yaml
solution:
  packs:
    - pack: ARM::CMSIS-MCP
      path: ./third_party/c_mcp
```

Adjust the path relative to your csolution.

To get the Python bridge, request the pack's `MCP-Host` layer in the
cproject. The layer provides `MCP_HOST_BRIDGE`; the project must consume that
connection to make the layer selectable:

```yaml
project:
  components:
    - component: CMSIS:MCP&Serial
  layers:
    - layer: $MCP-Host-Layer$
      type: MCP-Host
  connections:
    - connect: MCP application
      consumes:
        - MCP_HOST_BRIDGE
```

Leave `$MCP-Host-Layer$` undefined in the csolution initially. In VS Code,
run **CMSIS: Configure Solution**, select `MCP-Host`, and click **OK** to copy
the layer, Python bridge, requirements and usage guide to the proposed
solution-level `tools/mcp` directory. The IDE then sets the layer variable
under the active target. Selecting the firmware component alone does not copy
the host files. See the [host-layer guide](tools/README.md) for details.

The component requires `CMSIS:CORE`; select it in the application or board
layer. Build the solution with the CMSIS Solution tooling, such as the
Build action in the CMSIS Solution extension for VS Code. The selected
component supplies its sources and include paths automatically.

## Configure and integrate the firmware

The builder copies the `c_mcp_config.h` configuration template into the
project's RTE directory. Edit that copy with the Configuration Wizard:

- `C_MCP_ARENA_SIZE`: request memory capacity, default 32 KiB.
- `C_MCP_SERIAL_LINE_SIZE`: serial input buffer size, default 4096 bytes.
- `C_MCP_ENABLE_SERIAL_LOOP`: enable the supplied serial input loop.
- `C_MCP_ENABLE_VFS`: enable optional read-only resources, default enabled.

Include `mcp.h` for tool and resource registration, and `serial_transport.h`
for the serial input loop and board serial hooks.

Register tools with `add_tool()`, `add_argument()` and `set_tool_callback()`
during application startup. Register optional resource URIs and their read
callbacks with `add_resource()`. Check registration results and
`mcp_prepare()` before accepting requests. Registration strings must remain
valid until `free_tools()`.

For board serial I/O, provide strong implementations of
`mcp_serial_transport_init()`, `mcp_serial_transport_close()`,
`mcp_serial_getchar()` and `mcp_serial_send()` as needed. The default hooks
use stdio. Call `init_serial()` once and `process_serial()` from the main
loop; call `end_serial()` and `free_tools()` after stopping request handling.

Numeric arguments are expected to be finite values representable as a `double`.
Tool callbacks receive validated arguments in registration order and return
static or request-arena text. Resource reads invoke the callback registered
for the exact URI. Dispatch is single-threaded; complete response transmission
before returning from the sender.

See the [integration and API documentation](Documentation/html/integration.html)
for callback examples, argument validation, error handling and memory lifetimes.
Generate the HTML as described under pack maintenance below if it is not
present in your source checkout.

## Run the host bridge

Start one bridge from the application directory containing `tools/mcp`:

```sh
uv run --script tools/mcp/mcp_serial_bridge.py --port "<serial-port>"
```

Replace `<serial-port>` with your board's serial device: for example `COM3`
on Windows, `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux, or
`/dev/cu.usbmodem12345` on macOS. The bridge requires Python 3.10 or newer;
`uv` installs its inline dependencies automatically.

The shared MCP endpoint is `http://127.0.0.1:8765/mcp`. Close other serial
monitors before starting the bridge: it must be the port's only owner.
Ctrl+C stops it and releases the port.

From this library's source checkout, you can also run the bridge directly:

```sh
uv run --script tools/mcp_serial_bridge.py --port "<serial-port>"
```

For layer updates and using an existing Python environment without `uv`,
see the [host-layer guide](tools/README.md).

### Automate the board with Bash and curl

Scripts can send MCP JSON-RPC messages to the Python bridge without an AI
agent. Start the bridge, then save this example as `board-tool.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Start the Python bridge first, then send HTTP requests to its MCP endpoint.
url=http://127.0.0.1:8765/mcp
http=(-fsS --max-time 30 -H 'Content-Type: application/json'
      -H 'Accept: application/json, text/event-stream')

# Initialize MCP and extract the session ID from the HTTP response headers.
session_id=$(curl "${http[@]}" "$url" -D - -o /dev/null --data-binary \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"bash-script","version":"1.0.0"}}}' \
    | awk 'tolower($1) == "mcp-session-id:" {gsub("\r", "", $2); print $2}')
[[ -n "$session_id" ]] || { echo 'Missing MCP session ID' >&2; exit 1; }
http+=(-H "Mcp-Session-Id: $session_id" -H 'MCP-Protocol-Version: 2025-06-18')

# Complete initialization. This notification has no ID or response body.
curl "${http[@]}" "$url" --data-binary \
    '{"jsonrpc":"2.0","method":"notifications/initialized"}'

# Set the board gain to 2.0. The gain argument is a JSON number.
curl "${http[@]}" "$url" --data-binary \
    '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"setGain","arguments":{"gain":2.0}}}'
printf '\n'

# Close this HTTP session; the shared bridge stays running.
curl "${http[@]}" "$url" -X DELETE -o /dev/null
```

Run `bash board-tool.sh` to set the gain to `2.0`. This example assumes the
board firmware defines a `setGain` tool with a numeric `gain` argument.
Change the `gain` value in the final `tools/call` message to set another gain,
and adjust `url` if needed. No tool discovery is performed.

The bridge forwards the tool call over serial and returns the board's result.
The script prints the JSON reply; check its `error` field or `result.isError`
when using it in automated checks, since an MCP error can still have HTTP
status 200. Only Bash, curl and awk are needed.

### Connect an AI harness

To let an AI agent control the board, add an MCP server entry to your AI
harness's configuration, using Streamable HTTP and the Python bridge URL
`http://127.0.0.1:8765/mcp`. The configuration format depends on the harness.
Keep the bridge running so the harness can discover and call the firmware's
tools.

## Pack and documentation maintenance

From a source checkout, generate the API documentation and validated pack:

```sh
bash Documentation/Doxygen/gen_doc.sh
CMSIS_PDSC=/path/to/ARM.CMSIS.pdsc bash gen_pack.sh --no-preprocess
```

Requires Bash, Doxygen, PackChk and zip. Set `DOXYGEN` or `PACKCHK` if those
executables are not on PATH. `CMSIS_PACK_ROOT` can locate the CMSIS dependency
instead of `CMSIS_PDSC`. Without `--no-preprocess`, `gen_pack.sh` generates
the documentation before packing. The archive is written to
`output/ARM.CMSIS-MCP.0.1.0.pack`.

The pack workflow generates documentation and a pack archive, uploads the pack,
and publishes the HTML to the `gh-pages` branch. The Pages workflow deploys
the [development documentation](https://christophe0606.github.io/c_mcp/main/).

Host-test instructions are in `tests/README.md` in the source checkout.
The host-test workflow runs on Ubuntu for each push and pull request;
no board or simulator is required.

## License

CMSIS-MCP project files are licensed under Apache-2.0, except the bundled
`cJSON.c` and `cJSON.h`, which retain their MIT license. cJSON comes from
[Dave Gamble's cJSON repository](https://github.com/DaveGamble/cJSON).
The bundled version is modified for embedded use with finite numbers only;
the changes and numeric error returns are documented in `cJSON.h`.
See [LICENSE.md](LICENSE.md) for the complete terms and the cJSON exception.
