# Options

The following options are allowed for all `nix-env` operations, but may not always have an effect.

  - `--file` / `-f` *fileish*\
    Specifies the Nix expression (designated below as the *active Nix
    expression*) used by the `--install`, `--upgrade`, and `--query
    --available` operations to obtain derivations. The default is
    `~/.nix-defexpr`.

    *fileish* is interpreted the same as with [nix-build](../nix-build.md#fileish-syntax).
    See that section for complete details (`nix-build --help`), but in summary, a path argument may be one of:

    {{#include ../fileish-summary.md}}

  - `--profile` / `-p` *path*\
    Specifies the profile to be used by those operations that operate on
    a profile (designated below as the *active profile*). A profile is a
    sequence of user environments called *generations*, one of which is
    the *current generation*.

  - `--dry-run`\
    For the `--install`, `--upgrade`, `--uninstall`,
    `--switch-generation`, `--delete-generations` and `--rollback`
    operations, this flag will cause `nix-env` to print what *would* be
    done if this flag had not been specified, without actually doing it.

    `--dry-run` also prints out which paths will be
    [substituted](@docroot@/glossary.md) (i.e., downloaded) and which paths
    will be built from source (because no substitute is available).

  - `--system-filter` *system*\
    By default, operations such as `--query
                    --available` show derivations matching any platform. This option
    allows you to use derivations for the specified platform *system*.
