#include "mcp.h"
#include "stdio_transport.h"
static int echo(int argc, const char **message, const char **args)
{
    (void)argc; *message = args[0]; return 0;
}
int main(void)
{
    struct tool *tool = add_tool("echo", "Return the supplied text");
    if (!tool || set_tool_callback(tool, echo)) return 1;
    add_argument(tool, "text", TYPE_STR, "Text to return");
    if (mcp_prepare() || init_stdio()) return 1;
    while (process_stdio() != MCP_STDIO_EOF) {}
    end_stdio(); free_tools(); return 0;
}
