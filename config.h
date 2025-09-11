#ifndef config_h
#define config_h

#define _GNU_SOURCE
#include <stdatomic.h>

// Comment to enable HTTP
#define MCP_STDIO

#define MCP_PORT 8100

extern _Atomic int done;

extern void dispatch(const char *line,int fd);

extern void process_stdio();
extern int init_stdio();
extern void end_stdio();

extern void process_http();
extern int init_http();
extern void end_http();
extern void http_200_json(int cfd, const char* body);

extern void processing_loop();
#endif