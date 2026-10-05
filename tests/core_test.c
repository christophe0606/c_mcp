#include "mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)

static cJSON *reply;
static int sent, notifications, calls;
static char wire[65536];

static void capture(const char *json, int fd)
{
    CHECK(fd == 42);
    ++sent;
    cJSON_Delete(reply);
    reply = NULL;
    if (!json) { ++notifications; return; }
    CHECK(strlen(json) < sizeof(wire));
    strcpy(wire, json);
    CHECK(mcp_arena_used() > 0);
}

static void exchange(const char *json)
{
    size_t allocations = mcp_heap_allocations();
    wire[0] = 0;
    dispatch_with_sender(json, 42, capture);
    CHECK(mcp_arena_used() == 0);
    CHECK(mcp_heap_allocations() == allocations);
    if (wire[0]) { reply = cJSON_Parse(wire); CHECK(reply); }
}

cJSON *handle_tools_call(cJSON *id, cJSON *params)
{
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params, "exhaust"))) {
        CHECK(mcp_arena_alloc(C_MCP_ARENA_SIZE) == NULL);
        return err(id, MCP_INTERNAL_ERROR, "Exhausted");
    }
    ++calls;
    return ok(id, create_result_text("called"));
}

int main(void)
{
    struct tool *tool = add_tool("test", "Test");
    add_argument(tool, "count", TYPE_INT, "Count");
    CHECK(mcp_prepare() == 0);
    exchange("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(reply, "result");
    cJSON *tools = cJSON_GetObjectItemCaseSensitive(result, "tools");
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(tools, 0), "inputSchema");
    cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(props, "count"), "type");
    CHECK(cJSON_IsString(type) && strcmp(type->valuestring, "integer") == 0);
    exchange("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\"}");
    CHECK(sent == 2 && notifications == 1 && calls == 0 && reply == NULL);
    exchange("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\"}");
    CHECK(calls == 1 && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(reply, "result")));
    exchange("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"} trailing");
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(reply, "id")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(reply, "error");
    CHECK(cJSON_GetObjectItemCaseSensitive(error, "code")->valueint == MCP_PARSE_ERROR);
    {
        int i;
        for (i = 0; i < 1000; ++i)
            exchange("{\"jsonrpc\":\"2.0\",\"id\":\"escaped\\\"id\",\"method\":\"tools/call\"}");
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(reply, "id")->valuestring, "escaped\"id") == 0);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":\"oom\\\"id\",\"method\":\"tools/call\",\"params\":{\"exhaust\":true}}");
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(reply, "id")->valuestring, "oom\"id") == 0);
        error = cJSON_GetObjectItemCaseSensitive(reply, "error");
        CHECK(cJSON_GetObjectItemCaseSensitive(error, "code")->valueint == MCP_INTERNAL_ERROR);
        /* A string larger than the arena exercises parser exhaustion and reset. */
        char *large = malloc(C_MCP_ARENA_SIZE + 128);
        CHECK(large);
        strcpy(large, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ping\",\"params\":\"");
        size_t n = strlen(large);
        memset(large + n, 'x', C_MCP_ARENA_SIZE);
        strcpy(large + n + C_MCP_ARENA_SIZE, "\"}");
        exchange(large);
        free(large);
        error = cJSON_GetObjectItemCaseSensitive(reply, "error");
        CHECK(cJSON_GetObjectItemCaseSensitive(error, "code")->valueint == MCP_INTERNAL_ERROR);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"ping\"}");
        CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(reply, "result")));
    }
    free_tools();
    free_tools();
    CHECK(mcp_prepare() == 0);
    exchange("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}");
    result = cJSON_GetObjectItemCaseSensitive(reply, "result");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result, "tools")) == 0);
    cJSON_Delete(reply);
    free_tools();
    return 0;
}
