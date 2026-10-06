# Integration {#integration}

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

To get the Python bridge, also import the pack's `MCP-Host` layer,
`SerialBridge.clayer.yml`, with the CMSIS Solution IDE. Its proposed
destination is `tools/mcp` in the application. Importing the layer copies
the layer file, Python bridge, requirements file and usage guide together,
outside RTE.

In the cproject, select the serial component and add the copied host layer:

```yaml
project:
  components:
    - component: CMSIS:MCP&Serial
      define:
        - CJSON_NESTING_LIMIT: 16
  layers:
    - layer: ./tools/mcp/SerialBridge.clayer.yml
      type: MCP-Host
```

Adjust the layer path relative to the cproject.

When updating the pack, refresh the imported layer explicitly and review
the copied files.

The component requires `CMSIS:CORE`; select it in the application or board
layer. Build the solution with the CMSIS Solution tooling, such as the
Build action in the CMSIS Solution extension for VS Code. The selected
component supplies its sources and include paths automatically.

## Configure and integrate the firmware

The builder copies `Config/c_mcp_config.h` to the project's RTE directory.
Edit that copy with the Configuration Wizard:

- `C_MCP_ARENA_SIZE`: request memory capacity, default 32 KiB.
- `C_MCP_SERIAL_LINE_SIZE`: serial input buffer size, default 4096 bytes.
- `C_MCP_ENABLE_SERIAL_LOOP`: enable the supplied serial input loop.
- `C_MCP_ENABLE_VFS`: enable optional read-only resources, default enabled.

Include `mcp.h` for tool and resource registration, and `serial_transport.h`
for the serial input loop and board serial hooks.

Register tools with `add_tool()`, `add_argument()` and `set_tool_callback()`.
Register optional read-only resources with `add_resource()`. Keep registration
strings alive until `free_tools()`. Check all registration results and
`mcp_prepare()` before accepting input.

The [simple C example](@ref simple_example) shows tool registration, startup,
the serial request loop and cleanup together for a `setGain` tool.

Numeric arguments are expected to be finite values representable as a `double`.

Call `init_serial()` once, then `process_serial()` in the foreground main loop.
The parser preserves partial input and accepts CR, LF and CRLF. It discards
oversized lines or lost input through the next delimiter. At shutdown,
call `end_serial()` and `free_tools()`.

Override `mcp_serial_transport_init()`, `mcp_serial_transport_close()`,
`mcp_serial_getchar()` and `mcp_serial_send()` with strong board functions.
These functions can use any serial interface.
A nonblocking read returns `MCP_SERIAL_NO_DATA` when RX is empty;
`MCP_SERIAL_EOF` means closure, and `MCP_SERIAL_INPUT_LOST` means RX data loss.
Finish serial writes before returning from the sender; response text is borrowed
from the request arena. Do not retain it.

Dispatch is single-threaded and non-reentrant. The request arena defaults to
32 KiB and the serial line buffer to 4096 bytes. Persistent registration and
prepared response metadata use the heap at startup.

## Run the host bridge

The bridge exposes serial as a shared HTTP MCP endpoint on the computer.
It requires Python 3.10 or newer. From the csolution directory:

```sh
uv run --script tools/mcp/mcp_serial_bridge.py --port "<serial-port>"
```

Replace `<serial-port>` with your board's serial device: for example `COM3`
on Windows, `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux, or
`/dev/cu.usbmodem12345` on macOS.

The shared MCP endpoint is `http://127.0.0.1:8765/mcp`. Close other serial
monitors before starting the bridge: it must be the port's only owner.
Ctrl+C stops it and releases the port.

For an existing project environment:

```sh
uv pip install --python <venv-python> -r tools/mcp/requirements-mcp-serial-bridge.txt
```

The bridge can also be run directly from the pack checkout or an unpacked pack:

```sh
uv run --script tools/mcp_serial_bridge.py --port "<serial-port>"
```

The C implementation in this release is the Serial variant. A C HTTP variant
will be added in a future release.

The [Bash and curl example](@ref automation) shows how to initialize an MCP
session and call a known firmware tool through the bridge without
an AI agent.

### Connect an AI harness

To let an AI agent control the board, add an MCP server entry to your AI
harness's configuration, using Streamable HTTP and the Python bridge URL
`http://127.0.0.1:8765/mcp`. The configuration format depends on the harness.
Keep the bridge running so the harness can discover and call the firmware's
tools.
