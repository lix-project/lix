#pragma once
/** @file USDT based trace points
 *
 * These can be used with bpftrace or dtrace using their respective USDT trace
 * providers.
 *
 * See the .d files in each library for probe details.
 *
 * Example:
 *
 * ```
 * sudo bpftrace -e 'usdt:*:lix_store:filetransfer__read { printf("%s read %d\n", str(arg0), arg1); }'
 * ```
 */

#include "lix/config.h"

// The clang static analyzer is busted on these and throws reserved-identifier
// lints at the crimes they put in the headers. Oh well.
#if HAVE_DTRACE && !defined(__clang_analyzer__)
#define ENABLE_DTRACE 1
#define TRACE(body) body
#include <sys/sdt.h>
#else
#define ENABLE_DTRACE 0
#define TRACE(body)
#endif
