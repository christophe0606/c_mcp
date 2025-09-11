// mcp_stdio_server.c — minimal MCP stdio server (NDJSON, no Content-Length)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"

enum type
{
    TYPE_STR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL
};

struct argument
{
    const char *name;
    enum type type;          // "str", "int", "float", "bool"
    const char *description; // optional
    struct argument *next;
};

struct tool
{
    const char *name;
    const char *description;
    struct argument *arguments; // JSON schema as string
    struct tool *next;
};

static struct tool *tool_list = NULL; // linked list of registered tools

void add_argument(struct tool *tool,
                  const char *name,
                  enum type type,
                  const char *description)
{
    struct argument *arg = malloc(sizeof(struct argument));
    arg->name = name;
    arg->type = type;
    arg->description = description;
    arg->next = tool->arguments;
    tool->arguments = arg;
}

void free_arguments(struct argument *arg_list)
{
    struct argument *arg = arg_list;
    while (arg)
    {
        struct argument *next = arg->next;
        free(arg);
        arg = next;
    }
}

struct tool *add_tool(const char *name,
                      const char *description)
{
    struct tool *t = malloc(sizeof(struct tool));
    t->name = name;
    t->description = description;
    t->arguments = NULL;
    t->next = tool_list;
    tool_list = t;
    return (t);
}

void free_tools()
{
    struct tool *t = tool_list;
    while (t)
    {
        struct tool *next = t->next;
        free_arguments(t->arguments);
        free(t);
        t = next;
    }
    tool_list = NULL;
}

void add_arguments(cJSON *props,
                   cJSON *req,
                   struct tool *tool)
{
    struct argument *arg = tool->arguments;
    while (arg)
    {
        cJSON *jsonArg = cJSON_CreateObject();
        if (arg->type == TYPE_STR)
            cJSON_AddStringToObject(jsonArg, "type", "string");
        else if (arg->type == TYPE_INT || arg->type == TYPE_FLOAT)
            cJSON_AddStringToObject(jsonArg, "type", "number");
        else if (arg->type == TYPE_BOOL)
            cJSON_AddStringToObject(jsonArg, "type", "boolean");
        else
            cJSON_AddStringToObject(jsonArg, "type", "string"); // default to string

        if (arg->description != NULL)
        {
            cJSON_AddStringToObject(jsonArg, "description", arg->description);
        }
        cJSON_AddItemToObject(props, arg->name, jsonArg);
        cJSON_AddItemToArray(req, cJSON_CreateString(arg->name));
        arg = arg->next;
    }
}

cJSON *get_json_for_tool(struct tool *tool)
{

    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "name", tool->name);
    cJSON_AddStringToObject(t, "description", tool->description);
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_CreateObject();
    cJSON *req = cJSON_CreateArray();
    add_arguments(props, req, tool);
    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(schema, "required", req);
    cJSON_AddItemToObject(t, "inputSchema", schema); // camelCase per spec
    return (t);
}

#define PROTOCOL_VERSION "2025-06-18" // match spec

static void send_json(cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj); // single line, no pretty \n
    fputs(s, stdout);
    fputc('\n', stdout); // newline = message boundary
    fflush(stdout);
    free(s);
}

static cJSON *ok(cJSON *id, cJSON *result)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddItemToObject(m, "id", cJSON_Duplicate(id, 1));
    cJSON_AddItemToObject(m, "result", result);
    return m;
}

static cJSON *err(cJSON *id, int code, const char *msg)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    if (id)
        cJSON_AddItemToObject(m, "id", cJSON_Duplicate(id, 1));
    cJSON *e = cJSON_CreateObject();
    cJSON_AddNumberToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", msg);
    cJSON_AddItemToObject(m, "error", e);
    return m;
}

static cJSON *handle_initialize(cJSON *id, cJSON *params)
{
    (void)params;
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", PROTOCOL_VERSION);

    cJSON *caps = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON_AddBoolToObject(tools, "listChanged", 0);
    cJSON_AddItemToObject(caps, "tools", tools);
    cJSON_AddItemToObject(result, "capabilities", caps);

    cJSON *serverInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(serverInfo, "name", "c-mcp-stdio");
    cJSON_AddStringToObject(serverInfo, "version", "0.2.0");
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);

    return ok(id, result);
}

static cJSON *handle_tools_list(cJSON *id)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();

    struct tool *tool = tool_list;
    while (tool)
    {
        cJSON *t = get_json_for_tool(tool);
        cJSON_AddItemToArray(tools, t);
        tool = tool->next;
    }

    cJSON_AddItemToObject(result, "tools", tools);
    return ok(id, result);
}

static cJSON *create_result_text(const char *text)
{
    cJSON *res = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(res, "content", content);
    return res;
}

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

static cJSON *handle_tools_call(cJSON *id, cJSON *params)
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

static void dispatch(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root)
    {
        cJSON *e = err(NULL, -32700, "Parse error");
        send_json(e);
        cJSON_Delete(e);
        return;
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id"); // may be NULL for notifications
    cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    if (!cJSON_IsString(method) || !method->valuestring)
    {
        cJSON *e = err(id, -32600, "Invalid Request");
        send_json(e);
        cJSON_Delete(e);
        cJSON_Delete(root);
        return;
    }

    cJSON *resp = NULL;
    const char *m = method->valuestring;

    if (strcmp(m, "initialize") == 0)
    {
        resp = handle_initialize(id, params);
    }
    else if (strcmp(m, "tools/list") == 0)
    {
        resp = handle_tools_list(id);
    }
    else if (strcmp(m, "tools/call") == 0)
    {
        resp = handle_tools_call(id, params);
    }
    else if (strcmp(m, "notifications/initialized") == 0)
    {
        // Notification: do NOT respond
        cJSON_Delete(root);
        return;
    }
    else
    {
        resp = err(id, -32601, "Method not found");
    }

    send_json(resp);
    cJSON_Delete(resp);
    cJSON_Delete(root);
}

int main(void)
{
    struct tool *echoTool = add_tool("echo", "Echo input text");
    add_argument(echoTool, "text", TYPE_STR, "Text to echo");

    struct tool *addTool = add_tool("add", "Add two numbers");
    // Add arguments in reverse order (linked list)
    add_argument(addTool, "b", TYPE_FLOAT, "Second number");
    add_argument(addTool, "a", TYPE_FLOAT, "First number");

    // No stdout noise — only valid MCP messages on stdout.
    setvbuf(stdout, NULL, _IONBF, 0);

    char *line = NULL;
    size_t cap = 0;
    for (;;)
    {
        size_t n = getline(&line, &cap, stdin);
        if (n <= 0)
            break; // EOF
        // Trim trailing \r?\n
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (n == 0)
            continue;
        dispatch(line);
    }
    free(line);
    return 0;
}