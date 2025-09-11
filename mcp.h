#ifndef mcp_h
#define mcp_h
#include "cJSON.h"



enum type
{
    TYPE_STR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL
};

struct argument;
struct tool;

extern void dispatch(const char *line,int fd);
extern void add_argument(struct tool *tool,
                  const char *name,
                  enum type type,
                  const char *description);

extern struct tool *add_tool(const char *name,
                      const char *description);
extern cJSON *ok(cJSON *id, cJSON *result);
extern cJSON *err(cJSON *id, int code, const char *msg);
extern cJSON *create_result_text(const char *text);



#endif