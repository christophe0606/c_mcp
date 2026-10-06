#include "mcp.h"
#include "cJSON.h"
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

static int test_callback(int argc, const char **message, const char **args)
{
    CHECK(argc == 1);
    if (!strcmp(args[0], "-999")) {
        CHECK(mcp_arena_alloc(C_MCP_ARENA_SIZE) == NULL);
        *message = "Exhausted"; return MCP_INTERNAL_ERROR;
    }
    CHECK(!strcmp(args[0], "7"));
    ++calls;
    *message = "called"; return 0;
}

static int ordered_callback(int argc, const char **message, const char **args)
{
    char *output = mcp_arena_alloc(128);
    CHECK(argc == 3 && output);
    snprintf(output, 128, "%s|%s|%s", args[0], args[1], args[2]);
    *message = output; ++calls; return 0;
}
static int failing_callback(int argc, const char **message, const char **args)
{
    CHECK(argc == 0 && args == NULL); *message = "Execution failed"; return -1;
}
static int invalid_callback(int argc, const char **message, const char **args)
{
    (void)argc; (void)message; (void)args; return 0;
}

static int resource_calls;
static int resource_callback(const char **message)
{
    char *output = mcp_arena_alloc(80);
    CHECK(output);
    snprintf(output, 80, "sensor value %d", ++resource_calls);
    *message = output; return 0;
}
static int resource_failure(const char **message) { *message = "Sensor unavailable"; return -1; }

int main(void)
{
    {
        char output[32];
        cJSON *number = cJSON_CreateNumber(0.125);
        cJSON *text = cJSON_CreateString("value");
        CHECK(number && text);
        CHECK(cJSON_GetNumberValue(number) == 0.125);
        CHECK(cJSON_GetNumberValue(NULL) == 0.0);
        CHECK(cJSON_GetNumberValue(text) == 0.0);
        CHECK(cJSON_SetNumberHelper(NULL, 2.5) == 0.0);
        CHECK(cJSON_SetNumberValue(number, -2.5) == -2.5);
        CHECK(cJSON_GetNumberValue(number) == -2.5);
        CHECK(cJSON_PrintPreallocated(number, output, sizeof(output), 0));
        CHECK(strcmp(output, "-2.5") == 0);
        cJSON_Delete(number);
        cJSON_Delete(text);
    }
    struct tool *tool = add_tool("test", "Test");
    add_argument(tool, "count", TYPE_INT, "Count");
    CHECK(set_tool_callback(tool, test_callback) == 0);
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
    exchange("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"test\",\"arguments\":{\"count\":7}}}");
    CHECK(calls == 1 && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(reply, "result")));
    exchange("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"} trailing");
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(reply, "id")));
    cJSON *error = cJSON_GetObjectItemCaseSensitive(reply, "error");
    CHECK(cJSON_GetObjectItemCaseSensitive(error, "code")->valueint == MCP_PARSE_ERROR);
    {
        int i;
        for (i = 0; i < 1000; ++i)
            exchange("{\"jsonrpc\":\"2.0\",\"id\":\"escaped\\\"id\",\"method\":\"tools/call\",\"params\":{\"name\":\"test\",\"arguments\":{\"count\":7}}}");
        CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(reply, "id")->valuestring, "escaped\"id") == 0);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":\"oom\\\"id\",\"method\":\"tools/call\",\"params\":{\"name\":\"test\",\"arguments\":{\"count\":-999}}}");
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
        CHECK(cJSON_GetObjectItemCaseSensitive(reply, "id")->valueint == 9);
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
    reply = NULL;
    free_tools();
    {
        int previous;
        struct tool *ordered = add_tool("ordered", "Argument order");
        add_argument(ordered, "text", TYPE_STR, "Text");
        add_argument(ordered, "number", TYPE_FLOAT, "Number");
        add_argument(ordered, "flag", TYPE_BOOL, "Flag");
        CHECK(set_tool_callback(ordered, ordered_callback) == 0);
        CHECK(set_tool_callback(add_tool("failure", "Execution error"), failing_callback) == 0);
        CHECK(set_tool_callback(add_tool("invalid", "No output"), invalid_callback) == 0);
        CHECK(add_tool("missing", "No callback"));
        CHECK(mcp_prepare() == 0);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\",\"params\":{\"name\":\"ordered\",\"arguments\":{\"flag\":false,\"number\":2.5,\"text\":\"payload\"}}}");
        CHECK(strstr(wire, "payload|2.5|false"));
        previous = calls;
        exchange("{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\",\"params\":{\"name\":\"ordered\",\"arguments\":{\"flag\":1,\"number\":2.5,\"text\":\"payload\"}}}");
        CHECK(calls == previous && strstr(wire, "-32602"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"tools/call\",\"params\":{\"name\":\"failure\"}}");
        CHECK(strstr(wire, "\"isError\":true") && strstr(wire, "Execution failed"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"tools/call\",\"params\":{\"name\":\"ordered\",\"arguments\":{\"text\":\"next\",\"number\":-3,\"flag\":true}}}");
        CHECK(strstr(wire, "next|-3|true") && !strstr(wire, "isError"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":24,\"method\":\"tools/call\",\"params\":{\"name\":\"invalid\"}}");
        CHECK(strstr(wire, "-32603"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":25,\"method\":\"tools/call\",\"params\":{\"name\":\"missing\"}}");
        CHECK(strstr(wire, "-32603"));
        free_tools();
        cJSON_Delete(reply); reply = NULL;
    }
    {
        CHECK(mcp_prepare() == 0);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"initialize\"}");
        result = cJSON_GetObjectItemCaseSensitive(reply, "result");
        cJSON *capabilities = cJSON_GetObjectItemCaseSensitive(result, "capabilities");
#if C_MCP_ENABLE_VFS
        CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(capabilities, "resources")));
        free_tools();
        CHECK(add_resource("sensor://unit/value", "Value", "Sensor value", "text/plain", resource_callback));
        CHECK(add_resource("sensor://unit/error", "Error", NULL, NULL, resource_failure));
        CHECK(!add_resource("sensor://unit/value", "Duplicate", NULL, NULL, resource_callback));
        CHECK(mcp_prepare() == 0);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"resources/list\"}");
        CHECK(strstr(wire, "sensor://unit/value") && strstr(wire, "text/plain"));
        exchange("{\"jsonrpc\":\"2.0\",\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/value\"}}");
        CHECK(resource_calls == 0);
        {
            int i;
            for (i = 0; i < 100; ++i)
                exchange("{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/value\"}}");
        }
        CHECK(strstr(wire, "sensor value 100") && mcp_resource_lookup_steps() <= 2);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/missing\"}}");
        CHECK(strstr(wire, "-32002"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":34,\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/error\"}}");
        CHECK(strstr(wire, "-32603") && strstr(wire, "Sensor unavailable"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":35,\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/value\",\"arguments\":{}}}");
        CHECK(strstr(wire, "-32602") && resource_calls == 100);
        exchange("{\"jsonrpc\":\"2.0\",\"id\":36,\"method\":\"resources/read\",\"params\":{\"uri\":false}}");
        CHECK(strstr(wire, "-32602"));
        free_tools();
        CHECK(mcp_resource_index_height() == 0);
#else
        CHECK(!cJSON_GetObjectItemCaseSensitive(capabilities, "resources"));
        CHECK(!add_resource("sensor://unit/value", "Value", NULL, NULL, resource_callback));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"resources/list\"}");
        CHECK(strstr(wire, "-32601"));
        exchange("{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"resources/read\",\"params\":{\"uri\":\"sensor://unit/value\"}}");
        CHECK(strstr(wire, "-32601"));
        free_tools();
#endif
        cJSON_Delete(reply); reply = NULL;
    }
    {
        char names[257][32];
        struct tool *registered[257];
        int order, i;
        for (i = 0; i < 257; ++i) snprintf(names[i], sizeof(names[i]), "shared-prefix-tool-%03d", i);
        for (order = 0; order < 3; ++order) {
            for (i = 0; i < 257; ++i) {
                int n = order == 0 ? i : order == 1 ? 256 - i : (i * 37) % 257;
                registered[n] = add_tool(names[n], "Index test");
                CHECK(registered[n]);
                CHECK(mcp_tool_index_height() <= 11);
            }
            CHECK(add_tool(names[128], "Duplicate") == NULL);
            CHECK(add_tool("", "Empty") == NULL);
            for (i = 0; i < 257; ++i) {
                CHECK(find_tool(names[i]) == registered[i]);
                CHECK(mcp_tool_lookup_steps() <= 11);
            }
            CHECK(find_tool("shared-prefix-tool-999") == NULL);
            CHECK(mcp_tool_lookup_steps() <= 11);
            CHECK(find_tool(NULL) == NULL);
            free_tools();
            CHECK(mcp_tool_index_height() == 0);
        }
    }
#if C_MCP_ENABLE_VFS
    {
        char uris[257][40];
        int i;
        for (i = 0; i < 257; ++i) {
            snprintf(uris[i], sizeof(uris[i]), "sensor://unit/values/%03d", i);
            CHECK(add_resource(uris[i], "Value", NULL, NULL, resource_callback));
            CHECK(mcp_resource_index_height() <= 11);
        }
        CHECK(mcp_prepare() == 0);
        for (i = 0; i < 257; ++i) {
            char request[180];
            snprintf(request, sizeof(request), "{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"resources/read\",\"params\":{\"uri\":\"%s\"}}", uris[i]);
            exchange(request);
            CHECK(strstr(wire, "sensor value") && mcp_resource_lookup_steps() <= 11);
        }
        cJSON_Delete(reply); reply = NULL; free_tools();
    }
#endif
    return 0;
}
