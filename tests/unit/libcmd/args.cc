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

constexpr std::string_view INVALID_CHANNEL = "channel:example";
constexpr std::string_view CHANNEL_URL = "https://nixos.org/channels/example/nixexprs.tar.xz";

namespace nix
{

TEST(Arguments, lookupFileArg) {
    initNix();
    initLibExpr();

    std::string const unitDataPath = getEnv("_NIX_TEST_UNIT_DATA").value();
    // Meson should be allowed to pass us a relative path here tbh.
    auto const canonDataPath = CanonPath::fromCwd(unitDataPath);

    std::string const searchPathElem = fmt("example=%s", unitDataPath);

    SearchPath searchPath;
    searchPath.elements.push_back(SearchPath::Elem::parse(searchPathElem));

    auto store = openStore("dummy://");
    auto state = std::make_shared<Evaluator>(searchPath, store, store);

    SourcePath const foundUnitData = lookupFileArg(*state, "<example>");
    EXPECT_EQ(foundUnitData.path, canonDataPath);

    // lookupFileArg should not resolve <search paths> if anything else is before or after it.
    SourcePath const yepEvenSpaces = lookupFileArg(*state, " <example>");
    EXPECT_EQ(yepEvenSpaces.path, CanonPath::fromCwd(" <example>"));
    EXPECT_EQ(lookupFileArg(*state, "<example>/nixos").path, CanonPath::fromCwd("<example>/nixos"));

    try {
        lookupFileArg(*state, INVALID_CHANNEL);
    } catch (FileTransferError const & ex) {
        std::string_view const msg(ex.what());
        EXPECT_NE(msg.find(CHANNEL_URL), msg.npos);
    }

    SourcePath const normalFile = lookupFileArg(*state, unitDataPath);
    EXPECT_EQ(normalFile.path, canonDataPath);
}

}
