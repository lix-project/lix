---
name: http2
internalName: enableHttp2
type: bool
default: true
---
Whether to enable HTTP/2 support.
HTTP/2 support cannot be disabled if HTTP/3 support is enabled, the `http2` setting will be ignored in this case.
