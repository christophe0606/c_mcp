# CMSIS-MCP

CMSIS-MCP lets scripts and AI agents control an embedded application through
MCP messages. It is intended for development, debugging
and testing, rather than use in a final product. Application-level automation
complements the work done with a debugger.

The `ARM::CMSIS-MCP` pack provides the `CMSIS:MCP&Serial` firmware component.
A Python bridge shares the board's serial connection through an HTTP MCP
endpoint.

The `CMSIS:MCP&Serial` CMSIS-Pack component includes tool
registration and optional read-only resources. 

The transport sends newline-delimited
JSON-RPC using serial hooks and by default use stdio.

Follow these guides in order:

1. @subpage integration
2. @subpage simple_example
3. @subpage automation

The simple C example registers `setGain` and shows startup, message processing
and cleanup. The API is grouped into:

- [Tool registration and callbacks](@ref cmcp_tools)
- [Read-only virtual resources](@ref cmcp_resources)
- [Startup and request memory](@ref cmcp_memory)
- [Dispatch and response delivery](@ref cmcp_dispatch)
- [Serial input and I/O hooks](@ref cmcp_serial)
- [Diagnostic counters](@ref cmcp_diagnostics)

The host Python bridge lets clients share one serial connection through an
HTTP MCP endpoint. The C component in this release uses serial transport.

The pack's `MCP-Host` layer copies the bridge to `tools/mcp` in the application,
outside RTE; see the [integration guide](@ref integration).
The [automation guide](@ref automation) explains how to connect an AI harness
and how to call tools from scripts.

## License and third-party code

CMSIS-MCP project files are licensed under Apache-2.0. The bundled `cJSON.c`
and `cJSON.h` are sourced from
[Dave Gamble's cJSON repository](https://github.com/DaveGamble/cJSON) and retain
their MIT license and original copyright notices. See the pack's
[LICENSE.md](../../LICENSE.md) for both license texts.
