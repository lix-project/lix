---
name: initial-connect-timeout
internalName: initialConnectTimeout
type: unsigned long
default: 5
---
The timeout for the first attempt to establish connections for file transfers
such as tarball fetches or binary cache substitutions in seconds.

Lix increases the timeout per failed attempt via exponential backoff.
For attempt `i` (starting at `0`) the timeout is determined by

    timeout := min(max_connect_timeout, initial_connect_timeout * 2^i)

The value is capped by the option [`max-connect-timeout`](#conf-max-connect-timeout).

The option [`download-attempts`](#conf-download-attempts) controls how many
attempts to download a file there are before giving up.
