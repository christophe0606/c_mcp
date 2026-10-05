#include "mcp.h"
#include "stdio_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)
static int input[20000], count, position, reads;
static int init_status, opens, closes, sent, notifications, channel, explicit_sent;
static char response[2048];

/* Strong definitions, compiled separately from the library's weak defaults. */
int mcp_stdio_transport_init(void) { ++opens; return init_status; }
void mcp_stdio_transport_close(void) { ++closes; }
int mcp_stdio_getchar(void)
{
    ++reads;
    return position < count ? input[position++] : MCP_STDIO_EOF;
}
void mcp_stdio_send(const char *json, int fd)
{
    channel = fd;
    if (!json) { ++notifications; return; }
    CHECK(mcp_arena_used() > 0 && strlen(json) < sizeof(response));
    strcpy(response, json); ++sent;
}
static void explicit_sender(const char *json, int fd)
{
    CHECK(json && fd == 9); ++explicit_sent;
}
static void reset_input(void) { count = position = reads = 0; }
static void text(const char *s)
{
    while (*s) { CHECK(count < 20000); input[count++] = (unsigned char)*s++; }
}
static void event(int value) { CHECK(count < 20000); input[count++] = value; }
static int poll(void)
{
    int result = process_stdio();
    CHECK(mcp_arena_used() == 0);
    return result;
}

int main(void)
{
    size_t allocations;
    int i, previous;
    init_status = 17;
    CHECK(init_stdio() == 17 && opens == 1);
    init_status = 0;
    CHECK(init_stdio() == 0 && opens == 2);
    CHECK(mcp_prepare() == 0);
    allocations = mcp_heap_allocations();

    /* Empty RX preserves a partial line; CRLF must produce only one reply. */
    text("\n\r{\"jsonrpc\":\"2.0\","); event(MCP_STDIO_NO_DATA);
    text("\"id\":1,\"method\":\"ping\"}\r\n");
    CHECK(poll() == 0 && sent == 0);
    CHECK(poll() == 1 && sent == 1 && strstr(response, "\"id\":1"));
    CHECK(poll() == MCP_STDIO_EOF);

    /* A final unterminated line is handled once, then reports EOF. */
    reset_input(); text("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}");
    CHECK(poll() == 1 && sent == 2 && strstr(response, "\"id\":2"));
    CHECK(poll() == MCP_STDIO_EOF);

    /* Lost input and oversized/null-containing lines cannot execute fragments. */
    reset_input(); previous = sent;
    text("{\"jsonrpc\":\"2.0\","); event(MCP_STDIO_INPUT_LOST);
    text("\"id\":99,\"method\":\"ping\"}\n");
    text("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\n");
    CHECK(poll() == 2 && sent == previous);
    CHECK(poll() == 1 && strstr(response, "\"id\":3"));
    reset_input(); previous = sent;
    for (i = 0; i < C_MCP_STDIO_LINE_SIZE + 1; ++i) event('x');
    text("\n{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"ping\"}\n");
    CHECK(poll() == 2 && sent == previous);
    CHECK(poll() == 1 && strstr(response, "\"id\":4"));
    reset_input(); previous = sent;
    text("{\"jsonrpc\":\"2.0\","); event(0);
    text("\"id\":99,\"method\":\"ping\"}\n");
    text("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"ping\"}\n");
    CHECK(poll() == 2 && sent == previous);
    CHECK(poll() == 1 && strstr(response, "\"id\":5"));

    reset_input(); previous = sent;
    text("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
    CHECK(poll() == 1 && sent == previous && notifications == 1);
    reset_input();
    for (i = 0; i < 8192; ++i) event('\n');
    text("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"ping\"}\n");
    CHECK(poll() == 0 && reads == 8192);
    CHECK(poll() == 1 && strstr(response, "\"id\":6"));

    /* Default dispatch and NULL sender select the override, including its fd. */
    dispatch("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}", 42);
    CHECK(channel == 42 && strstr(response, "\"id\":7"));
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"ping\"}", 43, NULL);
    CHECK(channel == 43);
    previous = sent;
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ping\"}", 9, explicit_sender);
    CHECK(sent == previous && explicit_sent == 1);
    CHECK(mcp_heap_allocations() == allocations);
    end_stdio(); CHECK(closes == 1); free_tools();
    puts("strong overrides, framing, nonblocking RX, input loss and recovery passed");
    return 0;
}
