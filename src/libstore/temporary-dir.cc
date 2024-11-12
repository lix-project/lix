#include "lix/libstore/temporary-dir.hh"

#include "lix/libutil/file-system.hh"
#include "lix/libstore/globals.hh"

namespace nix {

Path createTempDir(const Path & tmpRoot, const Path & prefix,
    bool includePid, bool useGlobalCounter, mode_t mode)
{
    return createTempSubdir(tmpRoot.empty() ? defaultTempDir() : tmpRoot, prefix, includePid, useGlobalCounter, mode);
}

std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix)
{
    Path tmpl(defaultTempDir() + "/" + prefix + ".XXXXXX");
    // FIXME: use O_TMPFILE.
    AutoCloseFD fd(mkstemp(tmpl.data()));
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
