R"(

**Store URL format**: `https+mtls://...`

This store allows a binary cache to be accessed via the HTTPS
protocol with mutual TLS mandated.

Two parameters can be passed to the query string:

- `tls-certificate`, a path to the TLS client certificate (optional)
- `tls-private-key`, a path to the TLS private key backing the client certificate (required)

)"
