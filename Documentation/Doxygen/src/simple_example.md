# Simple C example: setGain {#simple_example}

This example shows the complete application sequence for one tool. It belongs
in a real project with the MCP component, configuration header and serial
implementation described in the [integration guide](@ref integration).
Enable `C_MCP_ENABLE_SERIAL_LOOP` to use the supplied input loop.

The `setGain` tool takes one numeric argument, `gain`, and updates an
application setting. Replace the assignment with the action your application
needs.

```c
#include "mcp.h"
#include "serial_transport.h"
#include <stdlib.h>

static double application_gain = 1.0;

static int set_gain(int argc, const char **returnMessage, const char **args)
{
    /* The dispatcher has already checked the required numeric argument. */
    (void)argc;
    double gain = strtod(args[0], NULL);

    if (gain < 0.0 || gain > 10.0) {
        *returnMessage = "gain must be between 0.0 and 10.0";
        return MCP_INVALID_PARAMS;
    }

    application_gain = gain;
    *returnMessage = "Gain updated"; /* Static text remains valid after return. */
    return 0;
}

int main(void)
{
    /* Register the tool, its required argument and its callback. */
    struct tool *tool = add_tool("setGain", "Set the application gain.");
    if (tool == NULL) {
        free_tools();
        return EXIT_FAILURE;
    }
    add_argument(tool, "gain", TYPE_FLOAT, "Gain between 0.0 and 10.0.");

    if (set_tool_callback(tool, set_gain) != 0) {
        free_tools();
        return EXIT_FAILURE;
    }

    /* Register any additional tools here, before preparing MCP. */

    /* Prepare MCP once all tools are registered; check before accepting input. */
    if (mcp_prepare() != 0) {
        free_tools();
        return EXIT_FAILURE;
    }
    if (init_serial() != 0) {
        free_tools();
        return EXIT_FAILURE;
    }

    /* Receive complete messages, dispatch them and send their responses. */
    while (process_serial() != MCP_SERIAL_EOF) {
        /* Other application work can use application_gain here. */
    }

    /* Stop serial I/O, then release MCP registrations and prepared data. */
    end_serial();
    free_tools();
    return EXIT_SUCCESS;
}
```

`process_serial()` assembles newline-delimited JSON messages and calls
`dispatch()` internally. A `tools/call` request for `setGain` invokes
`set_gain()`, with `args[0]` containing the numeric text. The callback returns
plain response text; MCP creates the JSON response and sends it through
`mcp_serial_send()`.

For example, the following message sets `application_gain` to `2.0`:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"setGain","arguments":{"gain":2.0}}}
```

The loop ends when the serial input closes. An embedded application can use
its own shutdown condition instead. Supply the project's serial hooks and
complete response transmission before the sender returns; see the
[serial API](@ref cmcp_serial). Continue with the
[automation guide](@ref automation) to connect an AI harness or call this tool
from a script through the Python bridge.
