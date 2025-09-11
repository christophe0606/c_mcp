#include <stdio.h>
#include <stdlib.h>
#include "config.h"

static char *line = NULL;

void init_stdio()
{
    // No stdout noise — only valid MCP messages on stdout.
    setvbuf(stdout, NULL, _IONBF, 0);

    line = NULL;
}

void end_stdio()
{
    free(line);
}

void process_stdio()
{
    size_t cap = 0;

    size_t n = getline(&line, &cap, stdin);
    if (n <= 0)
    {
        atomic_store(&done, 1);
        return; // EOF
    }
    // Trim trailing \r?\n
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = 0;
    if (n == 0)
        return;
    dispatch(line);
}