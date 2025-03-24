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

// NOTE: glib disables this for the clang static analyzer, idk if we need to also
#if HAVE_DTRACE
#define TRACE(body) body
#include <sys/sdt.h>
#else
#define TRACE(body)
#endif
