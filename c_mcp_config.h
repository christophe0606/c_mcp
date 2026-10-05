#ifndef C_MCP_CONFIG_H
#define C_MCP_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>
// <o> Request arena size in bytes <1024-1048576>
// <i> Parsing, callback output and serialization share this bounded arena.
// <i> Persistent metadata and response templates use the startup heap.
#ifndef C_MCP_ARENA_SIZE
#define C_MCP_ARENA_SIZE 32768
#endif
// <e> Enable read-only virtual files as MCP resources
// <i> Set to 0 to omit resource registries, schemas and request handlers.
#ifndef C_MCP_ENABLE_VFS
#define C_MCP_ENABLE_VFS 1
#endif
// </e>
// <<< end of configuration section >>>

#endif
