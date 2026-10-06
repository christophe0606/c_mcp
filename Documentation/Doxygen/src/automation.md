# Automate the board with Bash and curl {#automation}

Scripts can call board tools directly through the Python bridge using MCP
JSON-RPC messages over HTTP. No AI agent is required. Start the bridge as
described in the [integration guide](@ref integration), then save this
script as `board-tool.sh`:

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
board firmware defines a `setGain` tool with a numeric `gain` argument, as in
the [simple C example](@ref simple_example); the
library does not register it automatically. Change the `gain` value in the
final `tools/call` message to set another gain, and adjust `url` if needed.
No tool discovery is performed.

The Python bridge forwards the tool call over serial and returns the board's
result. The script prints the JSON response; check its `error` field or
`result.isError` when using it in automated checks, since an MCP error can
still have HTTP status 200. Only Bash, curl and awk are needed.

After initialization, reuse the same session ID and HTTP headers to call
multiple tools. Send those calls before the final `DELETE` request, which
closes the session.
