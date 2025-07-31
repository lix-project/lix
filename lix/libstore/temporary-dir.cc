#include "lix/libstore/temporary-dir.hh"

#include "lix/libutil/c-calls.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/globals.hh"

namespace nix {

Path createTempDir(const std::optional<Path> & prefix, mode_t mode)
{
    return createTempSubdir(defaultTempDir(), prefix, mode);
}

std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix)
{
    Path tmpl(defaultTempDir() + "/" + prefix + ".XXXXXX");
    // FIXME: use O_TMPFILE.
    AutoCloseFD fd(sys::mkstemp(tmpl));
    if (!fd)
        throw SysError("creating temporary file '%s'", tmpl);
    closeOnExec(fd.get());
    return {std::move(fd), tmpl};
}

Path defaultTempDir()
{
    return settings.tempDir.get().or_else([] {
        return getEnvNonEmpty("TMPDIR").and_then([](auto val) -> std::optional<Path> {
#if __APPLE__
            /* On macOS, don't use the per-session TMPDIR (as set e.g. by
               sshd). This breaks build users because they don't have access
               to the TMPDIR, in particular in ‘nix-store --serve’. */
            if (val.starts_with("/var/folders/")) {
                return std::nullopt;
            }
#endif
            return val;
        });
    }).value_or("/tmp");
}

}
