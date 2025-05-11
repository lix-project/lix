#include "lix/libstore/downstream-placeholder.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

std::string DownstreamPlaceholder::render() const
{
    return "/" + hash.to_string(Base::Base32, false);
}


DownstreamPlaceholder DownstreamPlaceholder::unknownCaOutput(
    const StorePath & drvPath,
    OutputNameView outputName,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::CaDerivations);
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    auto clearText = "nix-upstream-output:" + std::string { drvPath.hashPart() } + ":" + outputPathName(drvName, outputName);
    return DownstreamPlaceholder {
        hashString(HashType::SHA256, clearText)
    };
}

DownstreamPlaceholder DownstreamPlaceholder::fromSingleDerivedPathBuilt(
    const SingleDerivedPath::Built & b,
    const ExperimentalFeatureSettings & xpSettings)
{
    return DownstreamPlaceholder::unknownCaOutput(b.drvPath->path, b.output, xpSettings);
}

}
