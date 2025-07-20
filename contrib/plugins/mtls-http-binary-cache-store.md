R"(

**Store URL format**: `https+mtls://...`

This store allows a binary cache to be accessed via HTTPS with mutual TLS (client certificate authentication).

Both parameters are required:

- `tls-certificate`, a path to the TLS client certificate
- `tls-private-key`, a path to the TLS private key backing the client certificate

If you don't need mTLS, use `https://` instead.

)"
