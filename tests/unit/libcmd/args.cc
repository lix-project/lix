#include <iostream>
#include <memory>
#include <string_view>

#include <boost/core/demangle.hpp>
#include <gtest/gtest.h>

#include "common-eval-args.hh"
#include "eval.hh"
#include "filetransfer.hh"
#include "shared.hh"
#include "store-api.hh"

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
    auto statePtr = std::make_shared<EvalState>(searchPath, store, store);
    auto & state = *statePtr;

    SourcePath const foundUnitData = lookupFileArg(state, "<example>");
    EXPECT_EQ(foundUnitData.path, canonDataPath);

    // lookupFileArg should not resolve <search paths> if anything else is before or after it.
    SourcePath const yepEvenSpaces = lookupFileArg(state, " <example>");
    EXPECT_EQ(yepEvenSpaces.path, CanonPath::fromCwd(" <example>"));
    EXPECT_EQ(lookupFileArg(state, "<example>/nixos").path, CanonPath::fromCwd("<example>/nixos"));

    try {
        lookupFileArg(state, INVALID_CHANNEL);
    } catch (FileTransferError const & ex) {
        std::string_view const msg(ex.what());
        EXPECT_NE(msg.find(CHANNEL_URL), msg.npos);
    }

    SourcePath const normalFile = lookupFileArg(state, unitDataPath);
    EXPECT_EQ(normalFile.path, canonDataPath);
}

}
