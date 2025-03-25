#include <iostream>
#include <memory>
#include <string_view>

#include <boost/core/demangle.hpp>
#include <gtest/gtest.h>

#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/types.hh"

constexpr std::string_view INVALID_CHANNEL = "channel:example";
constexpr std::string_view CHANNEL_URL = "https://nixos.org/channels/example/nixexprs.tar.xz";

namespace nix
{

TEST(Arguments, lookupFileArg) {
    AsyncIoRoot aio;

    initNix();
    initLibExpr();

    std::string const unitDataPath = getEnv("_NIX_TEST_UNIT_DATA").value();
    // Meson should be allowed to pass us a relative path here tbh.
    auto const canonDataPath = CanonPath::fromCwd(unitDataPath);

    std::string const searchPathElem = fmt("example=%s", unitDataPath);

    SearchPath searchPath;
    searchPath.elements.push_back(SearchPath::Elem::parse(searchPathElem));

    auto store = aio.blockOn(openStore("dummy://"));
    auto state = std::make_shared<Evaluator>(aio, searchPath, store, store);

    SourcePath const foundUnitData =
        aio.blockOn(lookupFileArg(*state, "<example>")).unwrap(always_progresses);
    EXPECT_EQ(foundUnitData.canonical(), canonDataPath);

    // lookupFileArg should not resolve <search paths> if anything else is before or after it.
    SourcePath const yepEvenSpaces =
        aio.blockOn(lookupFileArg(*state, " <example>")).unwrap(always_progresses);
    EXPECT_EQ(yepEvenSpaces.canonical(), CanonPath::fromCwd(" <example>"));
    EXPECT_EQ(
        aio.blockOn(lookupFileArg(*state, "<example>/nixos")).unwrap(always_progresses).canonical(),
        CanonPath::fromCwd("<example>/nixos")
    );

    try {
        aio.blockOn(lookupFileArg(*state, INVALID_CHANNEL));
    } catch (FileTransferError const & ex) {
        std::string_view const msg(ex.what());
        EXPECT_NE(msg.find(CHANNEL_URL), msg.npos);
    }

    SourcePath const normalFile =
        aio.blockOn(lookupFileArg(*state, unitDataPath)).unwrap(always_progresses);
    EXPECT_EQ(normalFile.canonical(), canonDataPath);
}

}
