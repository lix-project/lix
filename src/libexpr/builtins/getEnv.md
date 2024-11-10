---
name: getEnv
args: [s]
---
`getEnv` returns the value of the environment variable *s*, or an
empty string if the variable doesn't exist. This function should be
used with care, as it can introduce all sorts of nasty environment
dependencies in your Nix expression.

`getEnv` is used in nixpkgs for evil impurities such as locating the file
`~/.config/nixpkgs/config.nix` which contains user-local settings for nixpkgs.
(That is, it does a `getEnv "HOME"` to locate the user's home directory.)

When in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval), this function always returns an empty string.
