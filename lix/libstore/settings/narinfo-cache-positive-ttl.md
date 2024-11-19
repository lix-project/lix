---
name: narinfo-cache-positive-ttl
internalName: ttlPositiveNarInfoCache
type: unsigned int
default: 2592000 # 30 * 24 * 3600
---
The TTL in seconds for positive lookups. If a store path is queried
from a substituter, the result of the query will be cached in the
local disk cache database including some of the NAR metadata. The
default TTL is a month, setting a shorter TTL for positive lookups
can be useful for binary caches that have frequent garbage
collection, in which case having a more frequent cache invalidation
would prevent trying to pull the path again and failing with a hash
mismatch if the build isn't reproducible.
