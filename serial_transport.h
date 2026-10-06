/**
 * \file serial_transport.h
 * \brief Serial line API with a default stdio implementation.
 */
#ifndef serial_transport_h
#define serial_transport_h
#include <c_mcp_config.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup cmcp_serial Portable serial transport
 * \ingroup cmcp
 * \brief Serial I/O with stdio defaults and weak functions on CMSIS boards.
 *
 * On boards, the defaults use CMSIS __WEAK from cmsis_compiler.h. Define any
 * mcp_serial_* hook below with the same signature in an application source file
 * to replace it. Do not add a weak annotation to the overrides. On Linux,
 * macOS and Windows the defaults are ordinary functions and use stdio only.
 * Defaults use standard C stdio, not POSIX APIs; no heap or atomic operations
 * are required. Calls are foreground-only, single-threaded and synchronous.
 * dispatch() and the default sender of dispatch_with_sender() use
 * mcp_serial_send(), including when the optional input loop is disabled.
 *
 * \par Nonblocking serial integration example for a CMSIS board
 * Supply the board-specific functions for the chosen serial interface.
 * \code{.c}
 * #include "serial_transport.h"
 * extern int board_serial_try_get_byte(void); // Byte, or -1 when RX is empty.
 * extern void board_serial_write_and_wait(const char *text);
 *
 * int mcp_serial_getchar(void) // Strong definition replaces the default.
 * {
 *     int byte = board_serial_try_get_byte();
 *     return byte < 0 ? MCP_SERIAL_NO_DATA : byte;
 * }
 * void mcp_serial_send(const char *json, int channel)
 * {
 *     (void)channel;
 *     if (json) {
 *         board_serial_write_and_wait(json);
 *         board_serial_write_and_wait("\n"); // NDJSON framing, including send completion.
 *     }
 * }
 * // Register tools/resources and call mcp_prepare() first.
 * // Then call init_serial(), process_serial() in the main loop, and end_serial().
 * \endcode
 * @{
 */

/** \brief Read hook result: input closed or unrecoverable input error. */
#define MCP_SERIAL_EOF (-1)
/** \brief Read hook result: no byte available yet; preserve the partial line. */
#define MCP_SERIAL_NO_DATA (-2)
/** \brief Read hook result: RX data lost; discard through the next delimiter. */
#define MCP_SERIAL_INPUT_LOST (-3)

/**
 * \brief Initialize the transport (weak default on CMSIS boards).
 * \return 0 on success, nonzero on failure; init_serial() forwards the result.
 * \details Default disables stdout buffering. Override if another interface
 * needs setup. It is not implicitly called by dispatch().
 */
int mcp_serial_transport_init(void);

/**
 * \brief Release transport resources (weak default on CMSIS boards).
 * \details Default does nothing and does not close stdin/stdout. Invoked by
 * end_serial(); not by free_tools().
 */
void mcp_serial_transport_close(void);

/**
 * \brief Obtain one input byte (weak default on CMSIS boards).
 * \return Byte in [0,255], MCP_SERIAL_NO_DATA, MCP_SERIAL_INPUT_LOST or MCP_SERIAL_EOF.
 * \details Default calls fgetc(stdin), blocking until input or EOF. Nonblocking
 * implementations return MCP_SERIAL_NO_DATA instead of EOF when RX is empty.
 */
int mcp_serial_getchar(void);

/**
 * \brief Synchronous sender (weak default on CMSIS boards).
 * \param[in] json Borrowed null-terminated JSON without a delimiter; NULL means
 *                 a notification, for which no JSON-RPC reply should be emitted.
 * \param[in] channel Opaque integer passed unchanged by the dispatcher.
 * \details Default writes to stdout, appends LF and flushes; channel is ignored.
 * Complete writes before returning, or copy into transport-owned storage. Do
 * not retain the JSON pointer: the request arena is reset after this returns.
 * An explicit dispatch_with_sender() callback takes precedence over this hook.
 */
void mcp_serial_send(const char *json, int channel);

#if C_MCP_ENABLE_SERIAL_LOOP
/**
 * \brief Reset line-buffer state and call mcp_serial_transport_init().
 * \return The transport initialization hook's status, zero for success.
 */
int init_serial(void);

/**
 * \brief Consume input until one line is processed, RX is empty or EOF occurs.
 * \return 1 after dispatching a nonempty line, 0 for idle/budget exhausted,
 *         2 after discarding a damaged/oversized line, or MCP_SERIAL_EOF on closure.
 *
 * Uses a static C_MCP_SERIAL_LINE_SIZE buffer, with no request heap allocation.
 * Preserves partial lines across calls; accepts LF, CR and CRLF. Discards
 * oversized input, embedded null bytes and RX loss through the next delimiter.
 * Reads at most 8192 bytes per call and dispatches at most one nonempty line.
 * A final unterminated line is dispatched at EOF; the next call returns
 * MCP_SERIAL_EOF. Applications decide whether EOF stops their main loop.
 * \note The input loop can be disabled with C_MCP_ENABLE_SERIAL_LOOP=0;
 * the I/O functions remain available.
 */
int process_serial(void);

/**
 * \brief Reset line-buffer state and call mcp_serial_transport_close().
 * \details Does not release registered tools/resources; use free_tools() too.
 */
void end_serial(void);
#endif
/** @} */
#ifdef __cplusplus
}
#endif
#endif
