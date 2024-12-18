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

DownstreamPlaceholder DownstreamPlaceholder::unknownDerivation(
    const DownstreamPlaceholder & placeholder,
    OutputNameView outputName,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::DynamicDerivations);
    auto compressed = compressHash(placeholder.hash, 20);
    auto clearText = "nix-computed-output:"
        + compressed.to_string(Base::Base32, false)
        + ":" + std::string { outputName };
    return DownstreamPlaceholder {
        hashString(HashType::SHA256, clearText)
    };
}

DownstreamPlaceholder DownstreamPlaceholder::fromSingleDerivedPathBuilt(
    const SingleDerivedPath::Built & b,
    const ExperimentalFeatureSettings & xpSettings)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & o) {
            return DownstreamPlaceholder::unknownCaOutput(o.path, b.output, xpSettings);
        },
        [&](const SingleDerivedPath::Built & b2) {
            return DownstreamPlaceholder::unknownDerivation(
                DownstreamPlaceholder::fromSingleDerivedPathBuilt(b2, xpSettings),
                b.output,
                xpSettings);
        },
    }, b.drvPath->raw());
}

}
