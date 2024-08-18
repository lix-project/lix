---
synopsis: HTTP proxy environment variables are now respected for S3 binary cache stores
issues: [fj#433]
cls: [1788]
category: Fixes
credits: jade
---

Due to "legacy reasons" (according to the AWS C++ SDK docs), the AWS SDK ignores system proxy configuration by default.
We turned it back on.
