#include "lix/libstore/builtins.hh"
#include "lix/libutil/tarfile.hh"

namespace nix {

void builtinUnpackChannel(const Path & out, const std::string & channelName, const std::string & src)
{
    createDirs(out);

    unpackTarfile(src, out);

    auto entries = readDirectory(out);
    if (entries.size() != 1)
        throw Error("channel tarball '%s' contains more than one file", src);
    renameFile((out + "/" + entries[0].name), (out + "/" + channelName));
}
}
