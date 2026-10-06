#include "mcp.h"
#include "serial_transport.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include "mcp_index.h"

/* The union supplies alignment for every scalar used by the portable core. */
static union { long double ld; void *ptr; long long integer;
    unsigned char bytes[C_MCP_ARENA_SIZE]; } arena;
static size_t arena_used, arena_peak, heap_allocations;
static int request_active, arena_failed, prepared, hooks_installed, heap_failed, registration_failed;
static cJSON *tools_result, *success_template, *error_template, *text_template;
static cJSON *success_id, *success_value, *error_id, *error_code, *error_message, *text_value;
static void install_hooks(void);
#if C_MCP_ENABLE_VFS
struct mcp_resource {
    struct mcp_index index;
    const char *name, *description, *mime_type;
    mcp_resource_fn callback;
    struct mcp_resource *next;
};
static struct mcp_resource *resource_list;
static struct mcp_index *resource_index;
static size_t resource_steps;
static cJSON *resources_result, *resource_template, *resource_uri, *resource_mime, *resource_text;
#endif

struct mcp_resource *add_resource(const char *uri, const char *name,
    const char *description, const char *mime_type, mcp_resource_fn callback)
{
#if C_MCP_ENABLE_VFS
    struct mcp_resource *resource;
    if (prepared || request_active || !uri || !*uri || !strchr(uri, ':') || !name || !*name ||
        !callback || index_find(resource_index, uri, NULL)) return NULL;
    install_hooks();
    resource = malloc(sizeof(*resource));
    if (!resource) { registration_failed = 1; return NULL; }
    resource->index.key = uri; resource->index.value = resource;
    resource->index.left = resource->index.right = NULL; resource->index.height = 1;
    resource->name = name; resource->description = description;
    resource->mime_type = mime_type ? mime_type : "text/plain";
    resource->callback = callback;
    resource->next = resource_list; resource_list = resource;
    resource_index = index_insert(resource_index, &resource->index);
    return resource;
#else
    (void)uri; (void)name; (void)description; (void)mime_type; (void)callback;
    return NULL;
#endif
}

size_t mcp_resource_index_height(void)
{
#if C_MCP_ENABLE_VFS
    return (size_t)index_height(resource_index);
#else
    return 0;
#endif
}
size_t mcp_resource_lookup_steps(void)
{
#if C_MCP_ENABLE_VFS
    return resource_steps;
#else
    return 0;
#endif
}

void *mcp_arena_alloc(size_t size)
{
    /* Align by the union's alignment using a small portable alignment probe. */
    struct alignment_probe { char byte; union { long double ld; void *ptr; long long i; } value; };
    const size_t align = offsetof(struct alignment_probe, value);
    size_t offset = arena_used + (align - arena_used % align) % align;
    if (!request_active || !size) return NULL;
    if (offset > C_MCP_ARENA_SIZE || size > C_MCP_ARENA_SIZE - offset) {
        arena_failed = 1; return NULL;
    }
    arena_used = offset + size;
    if (arena_used > arena_peak) arena_peak = arena_used;
    return arena.bytes + offset;
}

char *mcp_arena_strdup(const char *text)
{
    char *copy;
    size_t size;
    if (!text) return NULL;
    size = strlen(text) + 1;
    copy = (char *)mcp_arena_alloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

size_t mcp_arena_used(void) { return arena_used; }
size_t mcp_arena_high_water(void) { return arena_peak; }
size_t mcp_heap_allocations(void) { return heap_allocations; }

static void * CJSON_CDECL allocate_json(size_t size)
{
    void *result;
    if (request_active) return mcp_arena_alloc(size);
    ++heap_allocations;
    result = malloc(size);
    if (!result) heap_failed = 1;
    return result;
}

static void CJSON_CDECL release_json(void *ptr)
{
    uintptr_t value = (uintptr_t)ptr, begin = (uintptr_t)arena.bytes;
    if (value >= begin && value < begin + sizeof(arena.bytes)) return;
    free(ptr);
}

static void install_hooks(void)
{
    if (!hooks_installed) {
        cJSON_Hooks hooks = { allocate_json, release_json };
        cJSON_InitHooks(&hooks);
        hooks_installed = 1;
    }
}

static void reference_value(cJSON *slot, const cJSON *value)
{
    char *key = slot->string;
    cJSON *next = slot->next, *prev = slot->prev;
    memset(slot, 0, sizeof(*slot));
    if (value) *slot = *value;
    else slot->type = cJSON_NULL;
    slot->type |= cJSON_IsReference | cJSON_StringIsConst;
    slot->string = key; slot->next = next; slot->prev = prev;
}

static const char *skip_space(const char *p)
{
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static const char *string_end(const char *p)
{
    if (*p++ != '"') return NULL;
    while (*p) {
        if (*p == '"') return p + 1;
        if (*p++ == '\\') { if (!*p) return NULL; ++p; }
    }
    return NULL;
}

/* Find and parse only the top-level ID before parsing the full request. cJSON
 * deletes its partial root on OOM; this independent arena copy retains the ID
 * needed by a transport to correlate the exhaustion error. This lexical pass
 * never invokes callbacks, and malformed JSON still gets a null-ID parse error. */
static cJSON *preserve_request_id(const char *line)
{
    const char *p = skip_space(line);
    if (*p++ != '{') return NULL;
    while (*(p = skip_space(p)) && *p != '}') {
        const char *key = p, *end = string_end(p), *value;
        int is_id;
        if (!end) return NULL;
        is_id = end - key == 4 && memcmp(key, "\"id\"", 4) == 0;
        if (!is_id && memchr(key, '\\', (size_t)(end - key))) {
            cJSON *decoded = cJSON_ParseWithLengthOpts(key, (size_t)(end - key), NULL, 0);
            is_id = cJSON_IsString(decoded) && strcmp(decoded->valuestring, "id") == 0;
            cJSON_Delete(decoded);
        }
        p = skip_space(end);
        if (*p++ != ':') return NULL;
        value = p = skip_space(p);
        if (*p == '"') {
            p = string_end(p);
            if (!p) return NULL;
        } else if (*p == '{' || *p == '[') {
            unsigned depth = 0;
            do {
                if (*p == '"') { p = string_end(p); if (!p) return NULL; continue; }
                if (*p == '{' || *p == '[') ++depth;
                if (*p == '}' || *p == ']') --depth;
                ++p;
            } while (*p && depth);
            if (depth) return NULL;
        } else {
            while (*p && *p != ',' && *p != '}') ++p;
        }
        if (is_id) {
            const char *parsed_end = NULL;
            cJSON *id = cJSON_ParseWithLengthOpts(value, (size_t)(p - value), &parsed_end, 0);
            if (id && parsed_end && skip_space(parsed_end) == p &&
                (cJSON_IsString(id) || cJSON_IsNumber(id) || cJSON_IsNull(id))) return id;
            return NULL;
        }
        p = skip_space(p);
        if (*p == ',') ++p;
        else return NULL;
    }
    return NULL;
}

struct argument
{
    const char *name;
    enum type type;          // "str", "int", "float", "bool"
    const char *description; // optional
    struct argument *next;
};

struct tool
{
    struct mcp_index index;
    const char *name;
    const char *description;
    struct argument *arguments; // JSON schema as string
    struct tool *next;
    mcp_tool_fn callback;
};

static struct tool *tool_list = NULL; // linked list of registered tools
static struct mcp_index *tool_index;
static size_t lookup_steps;

struct tool *find_tool(const char *name)
{
    return (struct tool *)index_find(tool_index, name, &lookup_steps);
}
size_t mcp_tool_lookup_steps(void) { return lookup_steps; }
size_t mcp_tool_index_height(void) { return (size_t)index_height(tool_index); }

void add_argument(struct tool *tool,
                  const char *name,
                  enum type type,
                  const char *description)
{
    if (prepared || request_active) return;
    struct argument *arg, **tail;
    if (!tool || !name || !*name || type < TYPE_STR || type > TYPE_BOOL) { registration_failed = 1; return; }
    for (arg = tool->arguments; arg; arg = arg->next)
        if (!strcmp(arg->name, name)) { registration_failed = 1; return; }
    arg = malloc(sizeof(struct argument));
    if (!arg) { registration_failed = 1; return; }
    arg->name = name;
    arg->type = type;
    arg->description = description;
    arg->next = NULL;
    tail = &tool->arguments;
    while (*tail) tail = &(*tail)->next;
    *tail = arg;
}

int set_tool_callback(struct tool *tool, mcp_tool_fn callback)
{
    if (!tool || !callback || prepared || request_active) return -1;
    tool->callback = callback; return 0;
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
    if (prepared || request_active || !name || !*name || find_tool(name)) return NULL;
    install_hooks();
    struct tool *t = malloc(sizeof(struct tool));
    if (!t) { registration_failed = 1; return NULL; }
    t->name = name;
    t->description = description;
    t->arguments = NULL;
    t->callback = NULL;
    t->index.key = name; t->index.value = t;
    t->index.left = t->index.right = NULL; t->index.height = 1;
    tool_index = index_insert(tool_index, &t->index);
    t->next = tool_list;
    tool_list = t;
    return (t);
}

void free_tools()
{
    if (request_active) return;
#if C_MCP_ENABLE_VFS
    cJSON_Delete(resources_result); resources_result = NULL;
    cJSON_Delete(resource_template); resource_template = NULL;
    while (resource_list) {
        struct mcp_resource *next = resource_list->next;
        free(resource_list); resource_list = next;
    }
    resource_index = NULL; resource_steps = 0;
#endif
    cJSON_Delete(tools_result); tools_result = NULL;
    cJSON_Delete(success_template); success_template = NULL;
    cJSON_Delete(error_template); error_template = NULL;
    cJSON_Delete(text_template); text_template = NULL;
    prepared = 0; registration_failed = 0;
    struct tool *t = tool_list;
    while (t)
    {
        struct tool *next = t->next;
        free_arguments(t->arguments);
        free(t);
        t = next;
    }
    tool_list = NULL;
    tool_index = NULL; lookup_steps = 0;
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
        else if (arg->type == TYPE_INT)
            cJSON_AddStringToObject(jsonArg, "type", "integer");
        else if (arg->type == TYPE_FLOAT)
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

#if C_MCP_ENABLE_VFS
static int prepare_resources(void)
{
    struct mcp_resource *resource;
    cJSON *list, *item, *contents;
    cJSON_Delete(resources_result); cJSON_Delete(resource_template);
    resources_result = cJSON_CreateObject();
    list = cJSON_AddArrayToObject(resources_result, "resources");
    for (resource = resource_list; resource; resource = resource->next) {
        item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "uri", resource->index.key);
        cJSON_AddStringToObject(item, "name", resource->name);
        if (resource->description) cJSON_AddStringToObject(item, "description", resource->description);
        cJSON_AddStringToObject(item, "mimeType", resource->mime_type);
        if (!cJSON_AddItemToArray(list, item)) { cJSON_Delete(item); return -1; }
    }
    resource_template = cJSON_CreateObject();
    contents = cJSON_AddArrayToObject(resource_template, "contents");
    item = cJSON_CreateObject();
    cJSON_AddItemToArray(contents, item);
    resource_uri = cJSON_CreateStringReference("");
    resource_mime = cJSON_CreateStringReference("");
    resource_text = cJSON_CreateStringReference("");
    cJSON_AddItemToObject(item, "uri", resource_uri);
    cJSON_AddItemToObject(item, "mimeType", resource_mime);
    cJSON_AddItemToObject(item, "text", resource_text);
    return heap_failed || !list || !contents || !resource_uri || !resource_mime || !resource_text ? -1 : 0;
}
#endif

int mcp_prepare(void)
{
    struct tool *tool;
    cJSON *list;
    if (prepared) return 0;
    if (request_active || registration_failed) return -1;
    install_hooks();
    /* A failed preparation may be retried without leaking partial templates. */
    cJSON_Delete(tools_result); cJSON_Delete(success_template);
    cJSON_Delete(error_template); cJSON_Delete(text_template);
    tools_result = success_template = error_template = text_template = NULL;
    heap_failed = 0;
    tools_result = cJSON_CreateObject();
    list = cJSON_AddArrayToObject(tools_result, "tools");
    for (tool = tool_list; tool; tool = tool->next) {
        cJSON *json = get_json_for_tool(tool);
        if (!json || !cJSON_AddItemToArray(list, json)) { cJSON_Delete(json); return -1; }
    }
#if C_MCP_ENABLE_VFS
    if (prepare_resources() != 0) return -1;
#endif
    success_template = cJSON_CreateObject();
    cJSON_AddStringToObject(success_template, "jsonrpc", "2.0");
    success_id = cJSON_AddNullToObject(success_template, "id");
    success_value = cJSON_AddNullToObject(success_template, "result");
    error_template = cJSON_CreateObject();
    cJSON_AddStringToObject(error_template, "jsonrpc", "2.0");
    error_id = cJSON_AddNullToObject(error_template, "id");
    {
        cJSON *error = cJSON_AddObjectToObject(error_template, "error");
        error_code = cJSON_AddNumberToObject(error, "code", MCP_INTERNAL_ERROR);
        error_message = cJSON_CreateStringReference("");
        cJSON_AddItemToObject(error, "message", error_message);
    }
    text_template = cJSON_CreateObject();
    {
        cJSON *content = cJSON_AddArrayToObject(text_template, "content");
        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToArray(content, item);
        cJSON_AddStringToObject(item, "type", "text");
        text_value = cJSON_CreateStringReference("");
        cJSON_AddItemToObject(item, "text", text_value);
    }
    if (heap_failed || !list || !success_id || !success_value || !error_id || !error_code ||
        !error_message || !text_value) return -1;
    /* Slots reference arena values and must never own their startup key. */
    cJSON_free(success_id->string); success_id->string = (char *)"id";
    cJSON_free(success_value->string); success_value->string = (char *)"result";
    cJSON_free(error_id->string); error_id->string = (char *)"id";
    reference_value(success_id, NULL);
    reference_value(success_value, NULL);
    reference_value(error_id, NULL);
    prepared = 1;
    return 0;
}

#define PROTOCOL_VERSION "2025-06-18" // match spec


static void send_json(cJSON *obj, int cfd, mcp_send_fn send)
{
    size_t available = C_MCP_ARENA_SIZE - arena_used;
    size_t previous_peak = arena_peak;
    char *s = NULL;
    if (available > 16 && available <= INT_MAX) {
        /* Serialization consumes the remaining arena exactly once. */
        s = (char *)mcp_arena_alloc(available - 16);
    }
    if (s && obj && !arena_failed &&
        cJSON_PrintPreallocated(obj, s, (int)(available - 16), 0)) {
        arena_used = (size_t)(s - (char *)arena.bytes) + strlen(s) + 1;
        arena_peak = arena_used > previous_peak ? arena_used : previous_peak;
        send(s, cfd);
    } else {
        /* Independent emergency storage survives even a completely full arena.
         * Preserve the request id when it fits, otherwise fall back to null. */
        char emergency[1024];
        error_code->valuedouble = MCP_INTERNAL_ERROR;
        error_code->valueint = MCP_INTERNAL_ERROR;
        error_message->valuestring = (char *)"Request arena exhausted";
        if (!cJSON_PrintPreallocated(error_template, emergency, sizeof(emergency), 0)) {
            reference_value(error_id, NULL);
            cJSON_PrintPreallocated(error_template, emergency, sizeof(emergency), 0);
        }
        send(emergency, cfd);
    }
}

cJSON *ok(cJSON *id, cJSON *result)
{
    if (request_active) {
        reference_value(success_id, id);
        reference_value(error_id, id);
        reference_value(success_value, result);
        return success_template;
    }
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddItemToObject(m, "id", cJSON_Duplicate(id, 1));
    cJSON_AddItemToObject(m, "result", result);
    return m;
}

cJSON *err(cJSON *id, int code, const char *msg)
{
    if (request_active) {
        reference_value(error_id, id);
        error_code->valuedouble = code; error_code->valueint = code;
        error_message->valuestring = mcp_arena_strdup(msg);
        if (!error_message->valuestring) error_message->valuestring = (char *)"Request arena exhausted";
        return error_template;
    }
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddItemToObject(m, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
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
#if C_MCP_ENABLE_VFS
    {
        cJSON *resources = cJSON_AddObjectToObject(caps, "resources");
        cJSON_AddBoolToObject(resources, "subscribe", 0);
        cJSON_AddBoolToObject(resources, "listChanged", 0);
    }
#endif
    cJSON_AddItemToObject(result, "capabilities", caps);

    cJSON *serverInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(serverInfo, "name", "c-mcp");
    cJSON_AddStringToObject(serverInfo, "version", "0.2.0");
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);

    return ok(id, result);
}

static cJSON *handle_tools_list(cJSON *id)
{
    if (request_active) return ok(id, tools_result);
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

static cJSON *handle_ping(cJSON *id)
{
    return ok(id, cJSON_CreateObject());
}

cJSON *create_result_text(const char *text)
{
    if (request_active) {
        text_value->valuestring = mcp_arena_strdup(text);
        if (!text_value->valuestring) return NULL;
        return text_template;
    }
    cJSON *res = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(res, "content", content);
    return res;
}

static const char *argument_text(const cJSON *value, enum type type)
{
    char number[32];
    if (type == TYPE_STR) return cJSON_IsString(value) ? value->valuestring : NULL;
    if (type == TYPE_BOOL) return cJSON_IsBool(value) ? (cJSON_IsTrue(value) ? "true" : "false") : NULL;
    if (!cJSON_IsNumber(value) ||
        (type == TYPE_INT && floor(value->valuedouble) != value->valuedouble)) return NULL;
    snprintf(number, sizeof(number), "%.17g", value->valuedouble);
    return mcp_arena_strdup(number);
}

cJSON *handle_tools_call(cJSON *id, cJSON *params)
{
    struct tool *tool;
    struct argument *arg;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    const cJSON *input;
    const char **argv = NULL, *message = NULL;
    int argc = 0, position = 0, status;
    if (!cJSON_IsObject(params) || !cJSON_IsString(name) || (arguments && !cJSON_IsObject(arguments)))
        return err(id, MCP_INVALID_PARAMS, "Expected tool name and optional arguments object");
    tool = find_tool(name->valuestring);
    if (!tool) return err(id, MCP_INVALID_PARAMS, "Unknown tool");
    if (!tool->callback) return err(id, MCP_INTERNAL_ERROR, "Tool has no callback");
    for (arg = tool->arguments; arg; arg = arg->next) ++argc;
    if (argc) {
        argv = (const char **)mcp_arena_alloc((size_t)argc * sizeof(*argv));
        if (!argv) return err(id, MCP_INTERNAL_ERROR, "Request arena exhausted");
    }
    /* Reject undeclared and duplicate input keys before calling any tool. */
    for (input = arguments ? arguments->child : NULL; input; input = input->next) {
        for (arg = tool->arguments; arg; arg = arg->next)
            if (!strcmp(input->string, arg->name)) break;
        if (!arg || cJSON_GetObjectItemCaseSensitive(arguments, input->string) != input)
            return err(id, MCP_INVALID_PARAMS, "Unknown or duplicate tool argument");
    }
    for (arg = tool->arguments; arg; arg = arg->next) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(arguments, arg->name);
        argv[position] = argument_text(value, arg->type);
        if (!argv[position++]) return err(id, arena_failed ? MCP_INTERNAL_ERROR : MCP_INVALID_PARAMS,
                                         arena_failed ? "Request arena exhausted" : "Missing argument or incorrect type");
    }
    status = tool->callback(argc, &message, argv);
    if (status > 0 || !message) return err(id, MCP_INTERNAL_ERROR, "Invalid tool callback result");
    if (status <= MCP_INVALID_REQUEST && status >= MCP_PARSE_ERROR)
        return err(id, status, message);
    {
        cJSON *result = create_result_text(message);
        if (status < 0 && result) {
            /* Reference content without appending to the cached child's chain. */
            cJSON *owned = cJSON_CreateObject();
            cJSON_AddItemReferenceToObject(owned, "content", result->child);
            cJSON_AddBoolToObject(owned, "isError", 1);
            result = owned;
        }
        return ok(id, result);
    }
}

#if C_MCP_ENABLE_VFS
static cJSON *handle_resource_read(cJSON *id, cJSON *params)
{
    const cJSON *uri = cJSON_GetObjectItemCaseSensitive(params, "uri"), *input;
    const char *message = NULL;
    struct mcp_resource *resource;
    int status;
    if (!cJSON_IsObject(params) || !cJSON_IsString(uri))
        return err(id, MCP_INVALID_PARAMS, "Expected resource URI");
    for (input = params->child; input; input = input->next) {
        if ((!strcmp(input->string, "uri") && input != uri) ||
            (strcmp(input->string, "uri") && strcmp(input->string, "_meta")))
            return err(id, MCP_INVALID_PARAMS, "Resource reads accept only uri and optional metadata");
    }
    resource = (struct mcp_resource *)index_find(resource_index, uri->valuestring, &resource_steps);
    if (!resource) return err(id, MCP_RESOURCE_NOT_FOUND, "Resource not found");
    status = resource->callback(&message);
    if (status != 0 || !message)
        return err(id, status <= -32000 && status >= -32768 ? status : MCP_INTERNAL_ERROR,
                   message ? message : "Invalid resource callback result");
    resource_uri->valuestring = (char *)resource->index.key;
    resource_mime->valuestring = (char *)resource->mime_type;
    resource_text->valuestring = mcp_arena_strdup(message);
    return ok(id, resource_text->valuestring ? resource_template : NULL);
}
#endif

void dispatch(const char *line,int cfd)
{
    dispatch_with_sender(line, cfd, mcp_serial_send);
}

void dispatch_with_sender(const char *line, int cfd, mcp_send_fn send)
{
    if (!send) send = mcp_serial_send;
    if (request_active) {
        send("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Reentrant dispatch is unsupported\"}}", cfd);
        return;
    }
    if (mcp_prepare() != 0) {
        send("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"MCP startup allocation failed\"}}", cfd);
        return;
    }
    arena_used = 0; arena_failed = 0; request_active = 1;
    reference_value(error_id, NULL);
    cJSON *saved_id = preserve_request_id(line);
    cJSON *root = cJSON_ParseWithOpts(line, NULL, 1);
    if (!root)
    {
        cJSON *e = arena_failed ? err(saved_id, MCP_INTERNAL_ERROR, "Request arena exhausted") :
                                 err(NULL, MCP_PARSE_ERROR, "Parse error");
        send_json(e,cfd,send);
        goto finished;
    }
    
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id"); // may be NULL for notifications
    cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "jsonrpc");
    if (!cJSON_IsObject(root) || !cJSON_IsString(version) ||
        strcmp(version->valuestring, "2.0") != 0 ||
        !cJSON_IsString(method) ||
        (id && !cJSON_IsString(id) && !cJSON_IsNumber(id) && !cJSON_IsNull(id)))
    {
        cJSON *e = err(NULL, MCP_INVALID_REQUEST, "Invalid Request");
        send_json(e,cfd,send);
        goto finished;
    }

    /* Notifications never have responses; only requests may mutate settings. */
    if (!id) { send(NULL, cfd); goto finished; }
    reference_value(error_id, id);

    cJSON *resp = NULL;
    const char *m = method->valuestring;

    if (strcmp(m, "initialize") == 0)
    {
        resp = handle_initialize(id, params);
    }
    else if (strcmp(m, "ping") == 0)
    {
        resp = handle_ping(id);
    }
    else if (strcmp(m, "tools/list") == 0)
    {
        resp = handle_tools_list(id);
    }
    else if (strcmp(m, "tools/call") == 0)
    {
        resp = handle_tools_call(id, params);
    }
#if C_MCP_ENABLE_VFS
    else if (strcmp(m, "resources/list") == 0) {
        if ((params && !cJSON_IsObject(params)) || cJSON_GetObjectItemCaseSensitive(params, "cursor"))
            resp = err(id, MCP_INVALID_PARAMS, "Resource list is not paginated");
        else resp = ok(id, resources_result);
    }
    else if (strcmp(m, "resources/read") == 0) {
        resp = handle_resource_read(id, params);
    }
#endif
    else
    {
        resp = err(id, MCP_METHOD_NOT_FOUND, "Method not found");
    }

    send_json(resp,cfd,send);
finished:
    /* Drop every arena reference before releasing the request's storage. */
    reference_value(success_id, NULL);
    reference_value(success_value, NULL);
    reference_value(error_id, NULL);
    text_value->valuestring = (char *)"";
    error_message->valuestring = (char *)"";
#if C_MCP_ENABLE_VFS
    resource_text->valuestring = (char *)"";
#endif
    request_active = 0; arena_used = 0;
}
