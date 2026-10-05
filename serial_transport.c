#include <stdio.h>
#include "serial_transport.h"
#include "mcp.h"

/* Desktop builds use ordinary stdio functions. Board builds always have CMSIS. */
#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
#define MCP_SERIAL_WEAK
#else
#include "cmsis_compiler.h"
#define MCP_SERIAL_WEAK __WEAK
#endif

MCP_SERIAL_WEAK int mcp_serial_transport_init(void)
{
    return setvbuf(stdout, NULL, _IONBF, 0);
}

MCP_SERIAL_WEAK void mcp_serial_transport_close(void) {}

MCP_SERIAL_WEAK int mcp_serial_getchar(void)
{
    int ch = fgetc(stdin);
    return ch == EOF ? MCP_SERIAL_EOF : ch;
}

MCP_SERIAL_WEAK void mcp_serial_send(const char *json, int channel)
{
    (void)channel;
    if (!json) return;
    fputs(json, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

#if C_MCP_ENABLE_SERIAL_LOOP
#if C_MCP_SERIAL_LINE_SIZE < 2
#error "C_MCP_SERIAL_LINE_SIZE must include at least one byte and a null terminator"
#endif
static char line[C_MCP_SERIAL_LINE_SIZE];
static size_t length;
static int discard;

int init_serial(void)
{
    length = 0; discard = 0;
    return mcp_serial_transport_init();
}

void end_serial(void)
{
    length = 0; discard = 0;
    mcp_serial_transport_close();
}

int process_serial(void)
{
    unsigned budget;
    for (budget = 0; budget < 8192; ++budget) {
        int ch = mcp_serial_getchar();
        if (ch == MCP_SERIAL_NO_DATA) return 0;
        if (ch == MCP_SERIAL_EOF) {
            if (length && !discard) {
                line[length] = '\0'; length = 0;
                dispatch(line, 0); return 1;
            }
            length = 0; discard = 0; return MCP_SERIAL_EOF;
        }
        if (ch == MCP_SERIAL_INPUT_LOST || ch < 0 || ch > 255 || ch == 0) {
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
