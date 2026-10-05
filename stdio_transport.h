/**
 * \file stdio_transport.h
 * \brief Portable line transport with overridable stdio defaults.
 */
#ifndef stdio_transport_h
#define stdio_transport_h
#include "c_mcp_config.h"
#include "c_mcp_compiler.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup cmcp_stdio Portable stdio transport
 * \ingroup cmcp
 * \brief Replace weak I/O functions to use an application's serial interface.
 *
 * Define any of the mcp_stdio_* hooks below with the same signature in an
 * application source file. Do not add a weak annotation to the overrides.
 * Defaults use standard C stdio, not POSIX APIs; no heap or atomic operations
 * are required. Calls are foreground-only, single-threaded and synchronous.
 * dispatch() and the default sender of dispatch_with_sender() use
 * mcp_stdio_send(), including when the optional input loop is disabled.
 *
 * \par Nonblocking UART integration example
 * The board-specific functions here must be supplied by the application.
 * \code{.c}
 * #include "stdio_transport.h"
 * extern int uart_try_get_byte(void); // Byte, or -1 when RX is empty.
 * extern void uart_write_and_wait(const char *text);
 *
 * int mcp_stdio_getchar(void) // Strong definition replaces the default.
 * {
 *     int byte = uart_try_get_byte();
 *     return byte < 0 ? MCP_STDIO_NO_DATA : byte;
 * }
 * void mcp_stdio_send(const char *json, int channel)
 * {
 *     (void)channel;
 *     if (json) {
 *         uart_write_and_wait(json);
 *         uart_write_and_wait("\n"); // NDJSON framing, including send completion.
 *     }
 * }
 * // Register tools/resources and call mcp_prepare() first.
 * // Then call init_stdio(), process_stdio() in the main loop, and end_stdio().
 * \endcode
 * @{
 */

/** \brief Read hook result: input closed or unrecoverable input error. */
#define MCP_STDIO_EOF (-1)
/** \brief Read hook result: no byte available yet; preserve the partial line. */
#define MCP_STDIO_NO_DATA (-2)
/** \brief Read hook result: RX data lost; discard through the next delimiter. */
#define MCP_STDIO_INPUT_LOST (-3)

/**
 * \brief Weak hook to initialize the application's transport.
 * \return 0 on success, nonzero on failure; init_stdio() forwards the result.
 * \details Default disables stdout buffering. Override if another interface
 * needs setup. It is not implicitly called by dispatch().
 */
int mcp_stdio_transport_init(void);
C_MCP_WEAK_ALIAS(mcp_stdio_transport_init)

/**
 * \brief Weak hook to release application transport resources.
 * \details Default does nothing and does not close stdin/stdout. Invoked by
 * end_stdio(); not by free_tools().
 */
void mcp_stdio_transport_close(void);
C_MCP_WEAK_ALIAS(mcp_stdio_transport_close)

/**
 * \brief Weak hook to obtain one input byte.
 * \return Byte in [0,255], MCP_STDIO_NO_DATA, MCP_STDIO_INPUT_LOST or MCP_STDIO_EOF.
 * \details Default calls fgetc(stdin), blocking until input or EOF. Nonblocking
 * implementations return MCP_STDIO_NO_DATA instead of EOF when RX is empty.
 */
int mcp_stdio_getchar(void);
C_MCP_WEAK_ALIAS(mcp_stdio_getchar)

/**
 * \brief Weak synchronous sender used by dispatch() and process_stdio().
 * \param[in] json Borrowed null-terminated JSON without a delimiter; NULL means
 *                 a notification, for which no JSON-RPC reply should be emitted.
 * \param[in] channel Opaque integer passed unchanged by the dispatcher.
 * \details Default writes to stdout, appends LF and flushes; channel is ignored.
 * Complete writes before returning, or copy into transport-owned storage. Do
 * not retain the JSON pointer: the request arena is reset after this returns.
 * An explicit dispatch_with_sender() callback takes precedence over this hook.
 */
void mcp_stdio_send(const char *json, int channel);
C_MCP_WEAK_ALIAS(mcp_stdio_send)

#if C_MCP_ENABLE_STDIO_LOOP
/**
 * \brief Reset line-buffer state and call mcp_stdio_transport_init().
 * \return The transport initialization hook's status, zero for success.
 */
int init_stdio(void);

/**
 * \brief Consume input until one line is processed, RX is empty or EOF occurs.
 * \return 1 after dispatching a nonempty line, 0 for idle/budget exhausted,
 *         2 after discarding a damaged/oversized line, or MCP_STDIO_EOF on closure.
 *
 * Uses a static C_MCP_STDIO_LINE_SIZE buffer, with no request heap allocation.
 * Preserves partial lines across calls; accepts LF, CR and CRLF. Discards
 * oversized input, embedded null bytes and RX loss through the next delimiter.
 * Reads at most 8192 bytes per call and dispatches at most one nonempty line.
 * A final unterminated, nonempty line is dispatched at EOF; the next call returns
 * MCP_STDIO_EOF. Applications decide whether EOF stops their main loop.
 * \note Does not modify the legacy demo's global done flag. The input loop can
 * be disabled with C_MCP_ENABLE_STDIO_LOOP=0; the weak I/O hooks remain available.
 */
int process_stdio(void);

/**
 * \brief Reset line-buffer state and call mcp_stdio_transport_close().
 * \details Does not release registered tools/resources; use free_tools() too.
 */
void end_stdio(void);
#endif
/** @} */
#ifdef __cplusplus
}
#endif
#endif
