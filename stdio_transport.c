#include <stdio.h>
#include "stdio_transport.h"
#include "mcp.h"

C_MCP_WEAK int C_MCP_DEFAULT_NAME(mcp_stdio_transport_init)(void)
{
    return setvbuf(stdout, NULL, _IONBF, 0);
}

C_MCP_WEAK void C_MCP_DEFAULT_NAME(mcp_stdio_transport_close)(void) {}

C_MCP_WEAK int C_MCP_DEFAULT_NAME(mcp_stdio_getchar)(void)
{
    int ch = fgetc(stdin);
    return ch == EOF ? MCP_STDIO_EOF : ch;
}

C_MCP_WEAK void C_MCP_DEFAULT_NAME(mcp_stdio_send)(const char *json, int channel)
{
    (void)channel;
    if (!json) return;
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

#if C_MCP_ENABLE_STDIO_LOOP
#if C_MCP_STDIO_LINE_SIZE < 2
#error "C_MCP_STDIO_LINE_SIZE must include at least one byte and a null terminator"
#endif
static char line[C_MCP_STDIO_LINE_SIZE];
static size_t length;
static int discard;

int init_stdio(void)
{
    length = 0; discard = 0;
    return mcp_stdio_transport_init();
}

void end_stdio(void)
{
    length = 0; discard = 0;
    mcp_stdio_transport_close();
}

int process_stdio(void)
{
    unsigned budget;
    for (budget = 0; budget < 8192; ++budget) {
        int ch = mcp_stdio_getchar();
        if (ch == MCP_STDIO_NO_DATA) return 0;
        if (ch == MCP_STDIO_EOF) {
            if (length && !discard) {
                line[length] = '\0'; length = 0;
                dispatch(line, 0); return 1;
            }
            length = 0; discard = 0; return MCP_STDIO_EOF;
        }
        if (ch == MCP_STDIO_INPUT_LOST || ch < 0 || ch > 255 || ch == 0) {
            length = 0; discard = 1; continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (discard) { length = 0; discard = 0; return 2; }
            if (length) {
                line[length] = '\0'; length = 0;
                dispatch(line, 0); return 1;
            }
        } else if (!discard) {
            if (length < sizeof(line) - 1) line[length++] = (char)ch;
            else { length = 0; discard = 1; }
        }
    }
    return 0;
}
#endif
