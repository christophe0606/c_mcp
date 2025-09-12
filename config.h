#ifndef config_h
#define config_h

//#define _GNU_SOURCE
#include <stdatomic.h>

// Comment to enable HTTP
//#define MCP_STDIO


extern _Atomic int done;

extern void processing_loop();
#endif