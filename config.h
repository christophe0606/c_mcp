#ifndef config_h
#define config_h

#define _GNU_SOURCE
#include <stdatomic.h>

// Comment to enable HTTP
#define MCP_STDIO

extern _Atomic int done;

extern void dispatch(const char *line);

extern void process_stdio();
extern void init_stdio();
extern void end_stdio();

extern void processing_loop();
#endif