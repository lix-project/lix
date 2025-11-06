#include "lix/libutil/error-trace.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/position.hh"

namespace nix {

Trace Trace::fromDrv(std::shared_ptr<Pos> pos, std::string drvName)
{
    DrvTrace dt(drvName);

    HintFmt h(
        "while evaluating derivation '%s'\n"
        "  whose name attribute is located at %s",
        dt.drvName,
        *pos
    );

    return Trace{
        .pos = pos,
        .hint = h,
        .drvTrace = dt,
    };
}

Trace Trace::fromDrvAttr(std::shared_ptr<Pos> pos, std::string drvName, std::string attrOfDrv)
{
    DrvTrace dt(drvName);

    HintFmt h("while evaluating attribute '%s' of derivation '%s'", attrOfDrv, dt.drvName);

    return Trace{
        .pos = pos,
        .hint = h,
        .drvTrace = dt,
    };
}

std::partial_ordering operator<=>(Trace const & lhs, Trace const & rhs)
{
    // If either's position is nullptr, then we compare without dereferencing either.
    if (!lhs.pos || !rhs.pos) {
        return std::forward_as_tuple(lhs.pos != nullptr, lhs.hint.str())
            <=> std::forward_as_tuple(rhs.pos != nullptr, rhs.hint.str());
    }

    return std::forward_as_tuple(*lhs.pos, lhs.hint.str())
        <=> std::forward_as_tuple(*rhs.pos, rhs.hint.str());
}

}
