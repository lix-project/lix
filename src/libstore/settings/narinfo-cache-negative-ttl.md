---
name: narinfo-cache-negative-ttl
internalName: ttlNegativeNarInfoCache
type: unsigned int
default: 3600
---
The TTL in seconds for negative lookups. If a store path is queried
from a substituter but was not found, there will be a negative
lookup cached in the local disk cache database for the specified
duration.
