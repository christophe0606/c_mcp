/**
 * \file c_mcp_compiler.h
 * \brief Compiler support for overridable default transport functions.
 * \details Prefer CMSIS __WEAK on Arm. Standalone GCC/Clang and IAR builds
 * use their equivalent weak attributes. MSVC uses linker alternate names.
 * Define C_MCP_COMPILER_HEADER to a quoted compiler-header name when automatic
 * CMSIS header discovery is unavailable, or provide C_MCP_WEAK explicitly.
 */
#ifndef C_MCP_COMPILER_H
#define C_MCP_COMPILER_H

#if defined(C_MCP_COMPILER_HEADER)
#include C_MCP_COMPILER_HEADER
#elif (defined(__arm__) || defined(__thumb__) || defined(__ICCARM__) || defined(__ARMCC_VERSION)) && defined(__has_include)
#if __has_include("cmsis_compiler.h")
#include "cmsis_compiler.h"
#endif
#endif

#if defined(_MSC_VER) && !defined(__WEAK) && !defined(C_MCP_WEAK)
/* COFF has no equivalent C function attribute. Emit an alternate-name directive
 * in every caller so defaults are also found in a static library. Public
 * declarations are intentionally not weak: application definitions are strong.
 */
#define C_MCP_WEAK
#define C_MCP_DEFAULT_NAME(name) name##_default
#if defined(_M_IX86)
#define C_MCP_SYMBOL_PREFIX "_"
#else
#define C_MCP_SYMBOL_PREFIX ""
#endif
#define C_MCP_WEAK_ALIAS(name) __pragma(comment(linker, "/alternatename:" C_MCP_SYMBOL_PREFIX #name "=" C_MCP_SYMBOL_PREFIX #name "_default"))
#else
#ifndef C_MCP_WEAK
#if defined(__WEAK)
#define C_MCP_WEAK __WEAK
#elif defined(__GNUC__) || defined(__clang__)
#define C_MCP_WEAK __attribute__((weak))
#elif defined(__ICCARM__) || defined(__CC_ARM)
#define C_MCP_WEAK __weak
#else
#error "Provide C_MCP_WEAK or C_MCP_COMPILER_HEADER for this compiler"
#endif
#endif
#define C_MCP_DEFAULT_NAME(name) name
#define C_MCP_WEAK_ALIAS(name)
#endif
#endif
