/**
 * \file mcp.h
 * \brief Public API for the portable C MCP server.
 */

/**
 * \defgroup cmcp C MCP server
 * \brief Register tools and optional read-only resources, then serve JSON-RPC requests.
 *
 * \par Application sequence
 * - Register tools with add_tool(), add_argument() and set_tool_callback().
 *   Optionally register resources with add_resource().
 * - Call mcp_prepare() and check its result before accepting requests.
 * - Supply complete, null-terminated JSON messages to dispatch() or
 *   dispatch_with_sender(), one message at a time.
 * - Call free_tools() only after the request loop has stopped.
 *
 * Startup metadata uses the heap. Request parsing, callback output and
 * serialization share the bounded arena configured by \c C_MCP_ARENA_SIZE.
 * Reset occurs after the synchronous sender returns. Resource support is
 * controlled by \c C_MCP_ENABLE_VFS. Registration strings must remain valid
 * and unchanged until free_tools().
 *
 * \warning Dispatch is single-threaded and non-reentrant. cJSON allocation
 * hooks are process-wide: do not replace them or use cJSON concurrently
 * with dispatch.
 */

/** \defgroup cmcp_types Argument types and protocol errors
 *  \ingroup cmcp
 *  \brief Discovery schema types, opaque handles and protocol error constants.
 */
/** \defgroup cmcp_tools Tool registration and callbacks
 *  \ingroup cmcp
 *  \brief Describe tools for clients and bind validated inputs to callbacks.
 */
/** \defgroup cmcp_resources Read-only virtual resources
 *  \ingroup cmcp
 *  \brief Expose virtual text through resources/list and resources/read.
 */
/** \defgroup cmcp_memory Startup and request memory
 *  \ingroup cmcp
 *  \brief Prepare persistent metadata and manage bounded request storage.
 */
/** \defgroup cmcp_dispatch Request dispatch and transport
 *  \ingroup cmcp
 *  \brief Serve complete JSON-RPC messages with synchronous response delivery.
 */
/** \defgroup cmcp_diagnostics Memory and index diagnostics
 *  \ingroup cmcp
 *  \brief Read counters without allocating memory.
 */
/** \defgroup cmcp_responses Low-level response helpers
 *  \ingroup cmcp
 *  \brief Compatibility helpers; registered callbacks normally return plain text.
 */

#ifndef mcp_h
#define mcp_h
#include "cJSON.h"
#include "c_mcp_config.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * \ingroup cmcp_types
 * \brief JSON type used in a required argument's discovery schema.
 */
enum type
{
    TYPE_STR,   /**< JSON string; callback receives decoded text. */
    TYPE_INT,   /**< Finite integral JSON number; callback receives numeric text. */
    TYPE_FLOAT, /**< Finite JSON number; callback receives numeric text. */
    TYPE_BOOL   /**< JSON boolean; callback receives "true" or "false". */
};

/** \ingroup cmcp_types
 *  \brief JSON-RPC parse error: malformed JSON (-32700). */
#define MCP_PARSE_ERROR (-32700)
/** \ingroup cmcp_types
 *  \brief JSON-RPC invalid request: malformed envelope (-32600). */
#define MCP_INVALID_REQUEST (-32600)
/** \ingroup cmcp_types
 *  \brief JSON-RPC method not found (-32601). */
#define MCP_METHOD_NOT_FOUND (-32601)
/** \ingroup cmcp_types
 *  \brief JSON-RPC invalid parameters: missing, extra or invalid input (-32602). */
#define MCP_INVALID_PARAMS (-32602)
/** \ingroup cmcp_types
 *  \brief JSON-RPC internal error: allocation or callback failure (-32603). */
#define MCP_INTERNAL_ERROR (-32603)
/** \ingroup cmcp_types
 *  \brief MCP resource not found: unregistered URI (-32002). */
#define MCP_RESOURCE_NOT_FOUND (-32002)

/** \ingroup cmcp_types
 *  \brief Opaque argument metadata managed by the registry. */
struct argument;
/** \ingroup cmcp_types
 *  \brief Opaque tool handle returned by add_tool() or find_tool(). */
struct tool;
/** \ingroup cmcp_types
 *  \brief Opaque resource handle returned by add_resource(). */
struct mcp_resource;
/**
 * \ingroup cmcp_resources
 * \brief Callback invoked when resources/read resolves a registered URI.
 * \param[out] returnMessage Set *returnMessage to null-terminated static or arena text.
 * \return 0 on success. Nonzero codes in [-32768, -32000] are preserved as
 *         JSON-RPC errors; other nonzero statuses become MCP_INTERNAL_ERROR.
 *         Missing output is also an internal error.
 * \details There are no input arguments. Resource failures produce JSON-RPC
 * errors, unlike tool execution failures with isError. Output is borrowed until
 * dispatch completes, so do not return an automatic local buffer.
 * \sa add_resource for a complete callback example.
 */
typedef int (*mcp_resource_fn)(const char **returnMessage);

/**
 * \ingroup cmcp_resources
 * \brief Register an exact virtual URI and its read callback during startup.
 * \param[in] uri Unique, nonempty URI containing a scheme separator (colon).
 * \param[in] name Nonempty human-readable resource name.
 * \param[in] description Contents, units or interpretation; NULL omits this field.
 * \param[in] mime_type MIME type; NULL selects "text/plain".
 * \param[in] callback Non-NULL read callback.
 * \return Borrowed handle, or NULL if VFS is disabled, the definition is invalid,
 *         the URI is duplicated, allocation fails or registration is closed.
 *
 * resources/list exposes uri, name, description and mimeType. resources/read uses
 * a separate AVL index to select the callback; unknown URIs return
 * MCP_RESOURCE_NOT_FOUND. Matching is exact and case-sensitive, without path
 * normalization. Strings are borrowed until free_tools(). This interface exposes
 * virtual text, not a host filesystem; no writes, subscriptions or URI templates.
 *
 * \par Complete example: format a live value in request-arena memory
 * \code{.c}
 * #include "mcp.h"
 * #include <stdio.h>
 *
 * static unsigned sensor_value;
 *
 * static int read_sensor(const char **message)
 * {
 *     const size_t capacity = 48;
 *     char *text = (char *)mcp_arena_alloc(capacity);
 *     if (!text) { *message = "Arena exhausted"; return MCP_INTERNAL_ERROR; }
 *     int n = snprintf(text, capacity, "sensor value %u", sensor_value);
 *     if (n < 0 || (size_t)n >= capacity) {
 *         *message = "Sensor response formatting failed";
 *         return MCP_INTERNAL_ERROR;
 *     }
 *     *message = text; // Valid until the synchronous sender returns.
 *     return 0;
 * }
 *
 * static int register_sensor(void)
 * {
 * #if C_MCP_ENABLE_VFS
 *     if (!add_resource("sensor://board/value", "Sensor value",
 *             "Latest sensor measurement", "text/plain", read_sensor)) return -1;
 * #endif
 *     return mcp_prepare(); // Register any tools before this call too.
 * }
 * \endcode
 * Read this resource with:
 * \code{.json}
 * {"jsonrpc":"2.0","id":2,"method":"resources/read","params":{"uri":"sensor://board/value"}}
 * \endcode
 */
extern struct mcp_resource *add_resource(const char *uri, const char *name,
    const char *description, const char *mime_type, mcp_resource_fn callback);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read the height of the balanced resource-URI index.
 * \return Node levels; zero when empty or VFS is disabled.
 * \par Example
 * \code{.c}
 * size_t height = mcp_resource_index_height();
 * \endcode
 */
extern size_t mcp_resource_index_height(void);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read the comparison count of the latest resources/read lookup.
 * \return Comparisons; zero before a read, after free_tools() or with VFS disabled.
 * \par Example
 * \code{.c}
 * size_t comparisons = mcp_resource_lookup_steps(); // After a resource request.
 * \endcode
 */
extern size_t mcp_resource_lookup_steps(void);

/**
 * \ingroup cmcp_tools
 * \brief Callback invoked for a validated tools/call request.
 * \param[in] argc Number of registered arguments, validated before invocation.
 * \param[out] returnMessage Set *returnMessage to null-terminated response text.
 * \param[in] args Array of argc strings in add_argument() registration order.
 *                NULL when argc is zero; no extra sentinel element.
 * \return 0 for success. Codes -32700 through -32600 produce a JSON-RPC error.
 *         Other negative values produce a tool result with isError=true.
 *         Positive status or missing output produces MCP_INTERNAL_ERROR.
 *
 * All arguments are required. The core rejects missing, extra, duplicate and
 * incorrectly typed inputs before invocation, so argc is redundant for a
 * fixed-signature callback and a defensive count check is optional.
 * Strings are decoded JSON text, booleans are "true"/"false", and finite numbers
 * are JSON-compatible numeric text (possibly exponent notation). The callback
 * must validate ranges/enumerations before applying changes.
 *
 * \warning Input strings are borrowed for this request. Output must reference
 * static text or request-arena text, not an automatic local buffer. Never retain
 * input/output pointers after dispatch or free arena memory.
 * \sa add_tool for a complete callback and registration example.
 */
typedef int (*mcp_tool_fn)(int argc, const char **returnMessage, const char **args);
/**
 * \ingroup cmcp_tools
 * \brief Bind the callback invoked when a client calls a registered tool.
 * \param[in] tool Valid registered tool handle.
 * \param[in] callback Non-NULL callback.
 * \return 0 on success; -1 for NULL inputs or after preparation/during dispatch.
 * \details May replace a callback during startup. A tool without a callback can
 * appear in discovery, but calling it produces MCP_INTERNAL_ERROR.
 * \par Example
 * \code{.c}
 * if (set_tool_callback(tool, set_indicator) != 0) { free_tools(); return -1; }
 * \endcode
 */
extern int set_tool_callback(struct tool *tool, mcp_tool_fn callback);

/**
 * \ingroup cmcp_tools
 * \brief Accept a legacy boolean input in place of a required string argument.
 * \param[in] tool Valid tool handle, not yet prepared.
 * \param[in] name Existing canonical TYPE_STR argument name.
 * \param[in] alias Nonempty alternative key, distinct from other names/aliases.
 * \param[in] true_value String passed to the callback for a true alias input.
 * \param[in] false_value String passed to the callback for a false alias input.
 * \return 0 on success; -1 for invalid/conflicting definitions or closed registration.
 *
 * Discovery still advertises only the canonical string argument. A request may
 * supply either the canonical input or its boolean alias, never both. The alias
 * uses the canonical argument's position and does not increase argc.
 * All strings are borrowed until free_tools().
 * \par Example
 * \code{.c}
 * struct tool *aa = add_tool("antialiasing", "Select antialiasing coverage");
 * add_argument(aa, "mode", TYPE_STR, "none, partial or full");
 * int rc = set_boolean_argument_alias(aa, "mode", "on", "full", "none");
 * // {"on":true} gives args[0] = "full"; {"on":false} gives "none".
 * // Check rc, register a callback, then call mcp_prepare().
 * \endcode
 */
extern int set_boolean_argument_alias(struct tool *tool, const char *name,
                                     const char *alias, const char *true_value,
                                     const char *false_value);
/**
 * \ingroup cmcp_tools
 * \brief Look up a registered tool by exact, case-sensitive name.
 * \param[in] name Tool name; NULL is permitted and has no match.
 * \return Borrowed handle, or NULL if absent. Valid until free_tools().
 * \details Uses a balanced AVL index: O(log M) string comparisons for M tools;
 * each comparison costs O(L) in name length. Does not allocate memory.
 * \par Example
 * \code{.c}
 * struct tool *tool = find_tool("setIndicator");
 * \endcode
 */
extern struct tool *find_tool(const char *name);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read the comparison count of the latest tool-name lookup.
 * \return Number of string comparisons; zero for a NULL lookup or empty index.
 * \details Registration and tools/call also perform lookups and can update it.
 * \par Example
 * \code{.c}
 * struct tool *tool = find_tool("setIndicator");
 * size_t comparisons = mcp_tool_lookup_steps();
 * \endcode
 */
extern size_t mcp_tool_lookup_steps(void);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read the height of the balanced tool-name index.
 * \return Node levels; zero when empty, one for a single tool.
 * \par Example
 * \code{.c}
 * size_t height = mcp_tool_index_height();
 * \endcode
 */
extern size_t mcp_tool_index_height(void);

/**
 * \ingroup cmcp_memory
 * \brief Finish startup by caching discovery schemas and response templates.
 * \return 0 on success, including repeated calls after successful preparation;
 *         -1 for allocation/registration failure or an active unprepared request.
 * \details Successful preparation closes registration until free_tools().
 * Dispatch lazily prepares for older applications, but explicit preparation lets
 * startup detect errors before accepting requests. Installs process-wide cJSON
 * hooks and uses the heap, not the request arena.
 * \par Example
 * \code{.c}
 * if (mcp_prepare() != 0) { free_tools(); return -1; }
 * \endcode
 */
extern int mcp_prepare(void);
/**
 * \ingroup cmcp_memory
 * \brief Allocate aligned storage from the current request arena.
 * \param[in] size Number of bytes; must be greater than zero.
 * \return Borrowed storage, or NULL outside dispatch, for zero size or exhaustion.
 * \details Aligns for the core's scalar types. Exhaustion marks the request as
 * failed; dispatch sends a bounded internal error and subsequent requests can
 * recover. No individual free: reset occurs after the synchronous sender returns.
 * \par Example
 * \code{.c}
 * char *text = (char *)mcp_arena_alloc(64); // Inside a callback only.
 * \endcode
 * \sa add_resource for formatted text and allocation/error checks.
 */
extern void *mcp_arena_alloc(size_t size);
/**
 * \ingroup cmcp_memory
 * \brief Copy a null-terminated string into the current request arena.
 * \param[in] text Source string; NULL is permitted.
 * \return Borrowed copy including its null terminator, or NULL for NULL input,
 *         exhaustion or a call outside dispatch.
 * \details Follows the lifetime/exhaustion rules of mcp_arena_alloc().
 * \par Example
 * \code{.c}
 * *returnMessage = mcp_arena_strdup("Measurement ready"); // Inside a callback.
 * if (!*returnMessage) { *returnMessage = "Arena exhausted"; return MCP_INTERNAL_ERROR; }
 * return 0;
 * \endcode
 */
extern char *mcp_arena_strdup(const char *text);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read current arena consumption, including alignment padding.
 * \return Bytes in use; zero after dispatch returns.
 * \par Example
 * \code{.c}
 * size_t used = mcp_arena_used(); // Inspect inside a callback or sender.
 * \endcode
 */
extern size_t mcp_arena_used(void);
/**
 * \ingroup cmcp_diagnostics
 * \brief Read the arena's recorded high-water mark.
 * \return Peak recorded bytes for parsing, callback storage and serialized output.
 *         Exhaustion may record the whole configured capacity.
 * \details Persists across requests and free_tools(); no reset API.
 * \par Example
 * \code{.c}
 * size_t peak = mcp_arena_high_water();
 * \endcode
 */
extern size_t mcp_arena_high_water(void);
/**
 * \ingroup cmcp_diagnostics
 * \brief Count allocation attempts through the installed cJSON heap allocator.
 * \return Cumulative cJSON heap-allocation attempts outside dispatch.
 * \details Not a byte count or a count of every allocation. Direct malloc calls
 * for registry nodes are not included; failed cJSON allocations are counted.
 * Requests should not increase this counter after mcp_prepare() succeeds.
 * \par Example
 * \code{.c}
 * size_t before = mcp_heap_allocations(); // After successful mcp_prepare().
 * dispatch("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}", 0);
 * size_t after = mcp_heap_allocations(); // Expected to equal before.
 * \endcode
 */
extern size_t mcp_heap_allocations(void);

/**
 * \ingroup cmcp_dispatch
 * \brief Dispatch one JSON-RPC message and send the response to stdout.
 * \param[in] line Complete, null-terminated JSON text; must not be NULL.
 * \param[in] fd Compatibility parameter, ignored by the stdout sender.
 * \details Writes JSON followed by LF and flushes stdout. Retargeted stdout can
 * provide embedded UART output; the application supplies the input loop.
 * Notifications emit nothing and do not invoke tool/resource callbacks.
 * The request arena has been reset when this function returns.
 * \par Example
 * \code{.c}
 * dispatch("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", 0);
 * \endcode
 */
extern void dispatch(const char *line,int fd);

/**
 * \ingroup cmcp_dispatch
 * \brief Synchronous transport callback used by dispatch_with_sender().
 * \param[in] json Borrowed, null-terminated JSON without a newline; NULL denotes
 *                a notification with no JSON-RPC response.
 * \param[in] fd Opaque integer supplied by dispatch_with_sender(), passed unchanged.
 * \warning Complete transmission before returning. Do not retain the pointer or
 * dispatch another request from this callback. An asynchronous/DMA transport must
 * wait for completion or copy into transport-owned storage first.
 * \details UART/stdout emits nothing for NULL; an HTTP sender may return HTTP 202.
 * \par Example
 * \code{.c}
 * #include <stdio.h>
 * static void send_reply(const char *json, int channel)
 * {
 *     (void)channel;
 *     if (json) { puts(json); fflush(stdout); }
 * }
 * \endcode
 */
typedef void (*mcp_send_fn)(const char *json, int fd);
/**
 * \ingroup cmcp_dispatch
 * \brief Dispatch one JSON-RPC message through an application-provided sender.
 * \param[in] line Complete, null-terminated JSON text; must not be NULL.
 * \param[in] fd Opaque transport/channel identifier passed unchanged to send.
 * \param[in] send Synchronous response callback; NULL selects the stdout sender.
 * \details Handles initialize, ping, tools/list and tools/call, and optionally
 * resources/list and resources/read. Rejects malformed envelopes, trailing input
 * and unsupported methods. Notifications do not invoke tool/resource callbacks.
 * On arena exhaustion, independent bounded storage supplies an error; the next
 * request can recover. Reentrant requests receive an internal error.
 * \par Example
 * \code{.c}
 * dispatch_with_sender("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}",
 *                      7, send_reply); // send_reply receives channel 7.
 * \endcode
 */
extern void dispatch_with_sender(const char *line, int fd, mcp_send_fn send);
/**
 * \ingroup cmcp_tools
 * \brief Append one required argument to a tool's discovery schema.
 * \param[in] tool Valid handle returned by add_tool() or find_tool().
 * \param[in] name Nonempty input key, unique among the tool's names and aliases.
 * \param[in] type JSON input type: TYPE_STR, TYPE_INT, TYPE_FLOAT or TYPE_BOOL.
 * \param[in] description Meaning, units, limits or choices; NULL omits the
 *                       property's description.
 *
 * tools/list places the argument in \c inputSchema.properties and its name in
 * \c inputSchema.required. TYPE_INT advertises integer; TYPE_FLOAT advertises
 * number. Descriptions do not generate enum/minimum/maximum constraints: enforce
 * these in the callback. Callback order is the order of add_argument() calls.
 *
 * \note This function has no return value. Invalid definitions, duplicate names
 * or allocation failure during registration make mcp_prepare() fail. Calls after
 * successful preparation or during dispatch are ignored. Strings are borrowed
 * until free_tools().
 * \par Example
 * \code{.c}
 * add_argument(tool, "on", TYPE_BOOL, "true enables; false disables");
 * add_argument(tool, "color", TYPE_STR, "Colour name: red, green or blue");
 * // args[0] = on, args[1] = color.
 * \endcode
 * The colour property advertised in tools/list is:
 * \code{.json}
 * {"type":"string","description":"Colour name: red, green or blue"}
 * \endcode
 * \sa add_tool for the complete registration example.
 */
extern void add_argument(struct tool *tool,
                  const char *name,
                  enum type type,
                  const char *description);

/**
 * \ingroup cmcp_tools
 * \brief Register a tool name and its client-visible description during startup.
 * \param[in] name Unique, nonempty, case-sensitive name used in tools/call.
 * \param[in] description Human-readable behavior and effects; NULL omits this
 *                       field from tools/list.
 * \return Borrowed tool handle, or NULL for an invalid/duplicate name,
 *         allocation failure or registration after preparation/during dispatch.
 *
 * The description becomes the tool's \c description field in tools/list.
 * Explain what the tool changes/reads, side effects and when changes take effect.
 * Argument meanings, units and accepted values belong in add_argument()
 * descriptions. These strings are metadata: they are not parsed as commands or
 * schemas, and they do not implement validation.
 *
 * Register arguments and a callback before mcp_prepare(). All registration
 * strings are borrowed and must remain valid and unchanged until free_tools().
 * No callback or argument is created implicitly.
 *
 * \par Complete example: describe, validate and register a tool
 * \code{.c}
 * #include "mcp.h"
 * #include <string.h>
 *
 * static int indicator_on;
 * static unsigned indicator_rgb;
 *
 * static int set_indicator(int argc, const char **message, const char **args)
 * {
 *     unsigned rgb;
 *     // args[0] is on, args[1] is color, regardless of JSON key order.
 *     if (argc != 2) { // Optional; the core already validates required inputs.
 *         *message = "Expected on and color";
 *         return MCP_INVALID_PARAMS;
 *     }
 *     if (!strcmp(args[1], "red")) rgb = 0xff0000;
 *     else if (!strcmp(args[1], "green")) rgb = 0x00ff00;
 *     else if (!strcmp(args[1], "blue")) rgb = 0x0000ff;
 *     else {
 *         *message = "color must be red, green or blue";
 *         return MCP_INVALID_PARAMS;
 *     }
 *     // Validate everything before applying changes. A board's main loop can
 *     // consume these example settings; do not retain pointers from args.
 *     indicator_rgb = rgb;
 *     indicator_on = !strcmp(args[0], "true");
 *     *message = "Indicator settings updated"; // Static text is valid.
 *     return 0;
 * }
 *
 * static int register_indicator(void)
 * {
 *     struct tool *tool = add_tool("setIndicator",
 *         "Set the indicator colour and enable state. Changes apply on the next update.");
 *     if (!tool || set_tool_callback(tool, set_indicator) != 0) {
 *         free_tools();
 *         return -1;
 *     }
 *     add_argument(tool, "on", TYPE_BOOL,
 *         "true enables the indicator; false disables it");
 *     add_argument(tool, "color", TYPE_STR, "Colour name: red, green or blue");
 *     if (mcp_prepare() != 0) { // Also detects failed argument registration.
 *         free_tools();
 *         return -1;
 *     }
 *     return 0;
 * }
 * \endcode
 * A client can call this tool with the following request. Reversing the JSON
 * keys does not change callback argument order:
 * \code{.json}
 * {"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"setIndicator","arguments":{"color":"green","on":true}}}
 * \endcode
 */
extern struct tool *add_tool(const char *name,
                      const char *description);
/**
 * \ingroup cmcp_responses
 * \brief Wrap a result in a JSON-RPC success envelope.
 * \param[in] id Borrowed request ID; supply a valid cJSON ID, including JSON null.
 * \param[in] result Result object; borrowed during dispatch. Outside dispatch its
 *                   ownership transfers to the returned envelope.
 * \return Borrowed reusable template during dispatch, or heap-owned envelope
 *         outside dispatch.
 * \details Outside dispatch, id is deep-copied. During dispatch, id/result are
 * referenced and must remain valid until serialization. Do not delete or retain
 * the borrowed response; subsequent calls can overwrite it.
 * \par Example
 * \code{.c}
 * cJSON *id = cJSON_CreateNumber(1); // Outside dispatch.
 * cJSON *response = ok(id, create_result_text("Ready"));
 * // Use response; do not separately delete its adopted result.
 * cJSON_Delete(response);
 * cJSON_Delete(id);
 * \endcode
 */
extern cJSON *ok(cJSON *id, cJSON *result);
/**
 * \ingroup cmcp_responses
 * \brief Build a JSON-RPC error envelope.
 * \param[in] id Borrowed request ID; NULL selects JSON null.
 * \param[in] code Error code, usually an MCP_* constant.
 * \param[in] msg Non-NULL, null-terminated error text; copied by the helper.
 * \return Borrowed reusable template during dispatch, or heap-owned object outside.
 * \details Do not delete/retain the dispatch template; later calls may overwrite it.
 * \par Example
 * \code{.c}
 * cJSON *response = err(NULL, MCP_INVALID_PARAMS, "Expected colour name");
 * cJSON_Delete(response); // Only when called outside dispatch.
 * \endcode
 */
extern cJSON *err(cJSON *id, int code, const char *msg);
/**
 * \ingroup cmcp_responses
 * \brief Build an MCP tool result containing one text content item.
 * \param[in] text Non-NULL, null-terminated text to copy.
 * \return Borrowed reusable template during dispatch (NULL if arena copying fails),
 *         or heap-owned cJSON result outside dispatch.
 * \details Returns content with type=text, without a JSON-RPC envelope.
 * Use ok() to wrap it. Registered callbacks return plain text: the core invokes
 * this helper for them. Do not delete/retain the dispatch template.
 * \par Example
 * \code{.c}
 * cJSON *result = create_result_text("Ready");
 * cJSON_Delete(result); // Only when called outside dispatch.
 * \endcode
 */
extern cJSON *create_result_text(const char *text);
/**
 * \ingroup cmcp_responses
 * \brief Build legacy server metadata for the optional HTTP transport.
 * \return Newly constructed metadata: heap-owned outside dispatch, request-arena
 *         lifetime inside dispatch.
 * \details Returns mcpVersion, name, version and a boolean tools capability.
 * This is legacy metadata, not the standard initialize result or tools/list
 * schema. dispatch() does not route it as a JSON-RPC method.
 * \par Example
 * \code{.c}
 * cJSON *metadata = handle_fetch(); // Outside dispatch.
 * cJSON_Delete(metadata);
 * \endcode
 */
extern cJSON *handle_fetch();
/**
 * \ingroup cmcp_responses
 * \brief Internal tools/call handler retained in the public header for compatibility.
 * \param[in] id Borrowed JSON-RPC request ID.
 * \param[in] params Object with tool name and optional arguments object.
 * \return Borrowed success/error response with current request lifetime.
 * \pre Active request established by dispatch() or dispatch_with_sender().
 * \warning Applications must not call this handler directly or from callbacks.
 * It depends on the request arena and reusable templates. Register callbacks
 * instead of supplying an application implementation of this function.
 * \par Example of the supported entry point
 * \code{.c}
 * dispatch_with_sender(
 *     "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
 *     "\"params\":{\"name\":\"setIndicator\",\"arguments\":{\"on\":true,\"color\":\"red\"}}}",
 *     0, send_reply);
 * \endcode
 */
extern cJSON *handle_tools_call(cJSON *id, cJSON *params);
/**
 * \ingroup cmcp_memory
 * \brief Release tool/resource registries and cached responses; reopen registration.
 * \details Invalidates all handles. Borrowed registration strings are not freed.
 * Repeated calls are safe; a call during dispatch is ignored. Arena high-water
 * and cJSON heap-allocation counters are not reset.
 * \par Example
 * \code{.c}
 * free_tools(); // After stopping the request loop, not inside a callback.
 * \endcode
 */
extern void free_tools(void);

#ifdef __cplusplus
}
#endif

#endif
