#include "mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); exit(1); } } while (0)

static cJSON *reply;
static int sent, notifications, calls;

static void capture(const char *json, int fd)
{
    CHECK(fd == 42);
    ++sent;
    cJSON_Delete(reply);
    reply = NULL;
    if (!json) { ++notifications; return; }
    reply = cJSON_Parse(json);
    CHECK(reply != NULL);
}

cJSON *handle_tools_call(cJSON *id, cJSON *params)
{
    (void)params;
    ++calls;
    return ok(id, create_result_text("called"));
}

int main(void)
{
    struct tool *tool = add_tool("test", "Test");
    add_argument(tool, "count", TYPE_INT, "Count");
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", 42, capture);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(reply, "result");
    cJSON *tools = cJSON_GetObjectItemCaseSensitive(result, "tools");
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(tools, 0), "inputSchema");
    cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(props, "count"), "type");
    CHECK(cJSON_IsString(type) && strcmp(type->valuestring, "integer") == 0);
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\"}", 42, capture);
    CHECK(sent == 2 && notifications == 1 && calls == 0 && reply == NULL);
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\"}", 42, capture);
    CHECK(calls == 1 && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(reply, "result")));
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"} trailing", 42, capture);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(reply, "id")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(reply, "error");
    CHECK(cJSON_GetObjectItemCaseSensitive(error, "code")->valueint == MCP_PARSE_ERROR);
    free_tools();
    free_tools();
    dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}", 42, capture);
    result = cJSON_GetObjectItemCaseSensitive(reply, "result");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result, "tools")) == 0);
    cJSON_Delete(reply);
    return 0;
}
