#pragma once
///@file

#include "lix/libstore/fs-accessor.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/ref.hh"

#include <functional>

namespace nix {

struct Source;

/**
 * Return an object that provides access to the contents of a NAR
 * file.
 */
ref<FSAccessor> makeNarAccessor(std::string && nar);

ref<FSAccessor> makeNarAccessor(Source & source);

/**
 * Create a NAR accessor from a NAR listing (in the format produced by
 * listNar()). The callback getNarBytes(offset, length) is used by the
 * readFile() method of the accessor to get the contents of files
 * inside the NAR.
 */
typedef std::function<std::string(uint64_t, uint64_t)> GetNarBytes;

ref<FSAccessor> makeLazyNarAccessor(
    const std::string & listing,
    GetNarBytes getNarBytes);

/**
 * Write a JSON representation of the contents of a NAR (except file
 * contents).
 */
kj::Promise<Result<JSON>>
listNar(ref<FSAccessor> accessor, const Path & path, bool recurse);
JSON listNar(const nar_index::Entry & nar);

}
