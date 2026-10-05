#include "mcp.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>

static int tool_echo(int argc, const char **message, const char **args)
{
    (void)argc; *message = args[0]; return 0;
}
static int tool_add(int argc, const char **message, const char **args)
{
    char *output = mcp_arena_alloc(64);
    (void)argc;
    if (!output) { *message = "Request arena exhausted"; return MCP_INTERNAL_ERROR; }
    snprintf(output, 64, "%.17g", strtod(args[0], NULL) + strtod(args[1], NULL));
    *message = output; return 0;
}
void define_tools(void)
{
    struct tool *echo = add_tool("echo", "Echo input text");
    struct tool *add = add_tool("add", "Add two numbers");
    set_tool_callback(echo, tool_echo);
    add_argument(echo, "text", TYPE_STR, "Text to echo");
    set_tool_callback(add, tool_add);
    add_argument(add, "a", TYPE_FLOAT, "First number");
    add_argument(add, "b", TYPE_FLOAT, "Second number");
    mcp_prepare();
}
