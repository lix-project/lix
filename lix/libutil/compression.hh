#pragma once
///@file

#include "lix/libutil/async-io.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/serialise.hh"

#include <memory>
#include <string>

namespace nix {

struct CompressionSink : BufferedSink, FinishSink
{
    using BufferedSink::operator ();
    using BufferedSink::writeUnbuffered;
    using FinishSink::finish;
};

std::string decompress(const std::string & method, std::string_view in);

std::unique_ptr<Source>
makeDecompressionSource(const std::string & method, std::unique_ptr<Source> inner);

box_ptr<AsyncInputStream>
makeDecompressionStream(const std::string & method, box_ptr<AsyncInputStream> inner);

std::string compress(const std::string & method, std::string_view in, const bool parallel = false, int level = -1);

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel = false, int level = -1);

MakeError(UnknownCompressionMethod, Error);

MakeError(CompressionError, Error);

}
