#pragma once
/**
 * @file
 */

#include <compare>
#include <memory>
#include <string>
#include <optional>

#include "lix/libutil/fmt.hh"

namespace nix {

struct Pos;

/** @brief Information for a @ref Trace that encountered a derivation.
 *
 * This is used for summarizing the derivations involved in an eval error
 * at the end of a trace-print.
 */
struct DrvTrace
{
    std::string drvName;
    // TODO: include more structured information like "element 6 of nativeBuildInputs".

    DrvTrace() = delete;

    explicit DrvTrace(std::string drvName) : drvName(drvName) {}

    friend std::strong_ordering
    operator<=>(DrvTrace const & lhs, DrvTrace const & rhs) noexcept = default;
};

struct Trace;

struct Trace
{
    std::shared_ptr<Pos> pos;
    HintFmt hint;
    std::optional<DrvTrace> drvTrace;

    /** Construct a Trace and canned format message assuming a derivation's
     * position and name.
     */
    static Trace fromDrv(std::shared_ptr<Pos> pos, std::string drvName);

    /** Construct a Trace and canned format message assuming a derivation's
     * position, name, and the attribute of that derivation which caused the
     * trace.
     */
    static Trace fromDrvAttr(std::shared_ptr<Pos> pos, std::string drvName, std::string attrOfDrv);

    friend std::partial_ordering operator<=>(Trace const & lhs, Trace const & rhs);
};

}
