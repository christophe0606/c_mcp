// mcp_stdio_server.c — minimal MCP stdio server (NDJSON, no Content-Length)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include "cJSON.h"
#include "config.h"
#include "http.h"
#include "stdio_transport.h"
#include "tools.h"

_Atomic int done;
static volatile sig_atomic_t g_stop = 0;




void on_sigint(int sig) 
{ 
    (void)sig; g_stop = 1; 
}


int main(void)
{
    
    signal(SIGINT, on_sigint);

    define_tools();


    atomic_store(&done, 0);

    int err=0;
#if defined(MCP_STDIO)
    err=init_stdio();
#else
    err=init_http();
#endif

   if (err != 0)
   {
         fprintf(stderr, "Failed to initialize MCP client\n");
         return 1;
   }

    while ((atomic_load(&done) == 0) && (!g_stop))
    {
#if defined(MCP_STDIO)
        process_stdio();
#else
        process_http();
#endif
        processing_loop();
    }

#if defined(MCP_STDIO)
    end_stdio();
#else
    end_http();
#endif

    return 0;
}