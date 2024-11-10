---
name: sandbox-dev-shm-size
internalName: sandboxShmSize
platforms: [linux]
type: std::string
default: 50%
---
This option determines the maximum size of the `tmpfs` filesystem
mounted on `/dev/shm` in Linux sandboxes. For the format, see the
description of the `size` option of `tmpfs` in mount(8). The default
is `50%`.
