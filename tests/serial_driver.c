#include "mcp.h"
#include "serial_transport.h"
#include <stdio.h>
#include <stdlib.h>

/* Test-only stdio server fixture. */
static int echo(int argc, const char **message, const char **args)
{
    (void)argc; *message = args[0]; return 0;
}
static int add_numbers(int argc, const char **message, const char **args)
{
    char *output = mcp_arena_alloc(64);
    int written;
    (void)argc;
    if (!output) { *message = "Request arena exhausted"; return MCP_INTERNAL_ERROR; }
    written = snprintf(output, 64, "%.17g", strtod(args[0], NULL) + strtod(args[1], NULL));
    if (written < 0 || written >= 64) {
        *message = "Result formatting failed"; return MCP_INTERNAL_ERROR;
    }
    *message = output; return 0;
}
int main(void)
{
    struct tool *tool = add_tool("echo", "Return the supplied text");
    struct tool *add = add_tool("add", "Add two numbers");
    if (!tool || set_tool_callback(tool, echo)) return 1;
    add_argument(tool, "text", TYPE_STR, "Text to return");
    if (!add || set_tool_callback(add, add_numbers)) return 1;
    add_argument(add, "a", TYPE_FLOAT, "First number");
    add_argument(add, "b", TYPE_FLOAT, "Second number");
    if (mcp_prepare() || init_serial()) return 1;
    while (process_serial() != MCP_SERIAL_EOF) {}
    end_serial(); free_tools(); return 0;
}
