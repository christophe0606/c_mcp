#ifndef mcp_h
#define mcp_h
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif



enum type
{
    TYPE_STR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL
};

#define MCP_PARSE_ERROR (-32700)
#define MCP_INVALID_REQUEST (-32600)
#define MCP_METHOD_NOT_FOUND (-32601)
#define MCP_INVALID_PARAMS (-32602)
#define MCP_INTERNAL_ERROR (-32603)

struct argument;
struct tool;

extern void dispatch(const char *line,int fd);
/* The callback consumes the JSON synchronously; it must not retain the pointer.
 * NULL means a valid notification: no JSON-RPC response (HTTP may send 202).
 * The core owns the JSON string. fd is passed through unchanged.
 */
typedef void (*mcp_send_fn)(const char *json, int fd);
extern void dispatch_with_sender(const char *line, int fd, mcp_send_fn send);
extern void add_argument(struct tool *tool,
                  const char *name,
                  enum type type,
                  const char *description);

extern struct tool *add_tool(const char *name,
                      const char *description);
extern cJSON *ok(cJSON *id, cJSON *result);
extern cJSON *err(cJSON *id, int code, const char *msg);
extern cJSON *create_result_text(const char *text);
extern cJSON *handle_fetch();
extern cJSON *handle_tools_call(cJSON *id, cJSON *params);
extern void free_tools(void);

#ifdef __cplusplus
}
#endif

#endif
