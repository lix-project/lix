---
name: pre-build-hook
internalName: preBuildHook
type: std::string
default: ''
---
If set, the path to a program that can set extra derivation-specific
settings for this system. This is used for settings that can't be
captured by the derivation model itself and are too variable between
different versions of the same system to be hard-coded into nix.

At the time of running the hook, the derivation may or may not exist on disk (and, e.g. won't exist in the case of many remote builds).

The hook is passed the derivation path and, if sandboxes are
enabled, the sandbox directory. It can then modify the sandbox and
send a series of commands to modify various settings to stdout. The
currently recognized commands are:

  - `extra-sandbox-paths`\
    Pass a list of files and directories to be included in the
    sandbox for this build. One entry per line, terminated by an
    empty line. Entries have the same format as `sandbox-paths`.
