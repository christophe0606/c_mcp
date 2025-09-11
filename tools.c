#include "config.h"
#include "mcp.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *tool_echo(cJSON *args)
{
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(args, "text");
    const char *s = (cJSON_IsString(text) && text->valuestring) ? text->valuestring : "";
    cJSON *res = create_result_text(s);
    return res;
}

static cJSON *tool_add(cJSON *args)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(args, "a");
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(args, "b");
    double ad = cJSON_IsNumber(a) ? a->valuedouble : 0.0;
    double bd = cJSON_IsNumber(b) ? b->valuedouble : 0.0;
    double sum = ad + bd;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", sum);

    cJSON *res = create_result_text(buf);

    return res;
}

cJSON *handle_tools_call(cJSON *id, cJSON *params)
{
    if (!cJSON_IsObject(params))
        return err(id, -32602, "Invalid params");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    if (!cJSON_IsString(name) || !name->valuestring)
        return err(id, -32602, "Missing tool name");
    if (!cJSON_IsObject(arguments))
        return err(id, -32602, "Missing arguments");

    cJSON *result = NULL;
    if (strcmp(name->valuestring, "echo") == 0)
    {
        result = tool_echo((cJSON *)arguments);
    }
    else if (strcmp(name->valuestring, "add") == 0)
    {
        result = tool_add((cJSON *)arguments);
    }
    else
    {
        return err(id, -32601, "Unknown tool");
    }
    return ok(id, result);
}

void define_tools()
{
    struct tool *echoTool = add_tool("echo", "Echo input text");
    add_argument(echoTool, "text", TYPE_STR, "Text to echo");

    struct tool *addTool = add_tool("add", "Add two numbers");
    // Add arguments in reverse order (linked list)
    add_argument(addTool, "b", TYPE_FLOAT, "Second number");
    add_argument(addTool, "a", TYPE_FLOAT, "First number");
}