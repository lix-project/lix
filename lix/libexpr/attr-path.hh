#pragma once
///@file

#include "lix/libexpr/eval.hh"

#include <string>

namespace nix {

MakeError(AttrPathNotFound, Error);
MakeError(NoPositionInfo, Error);

std::pair<Value, PosIdx> findAlongAttrPath(
    EvalState & state, const std::string & attrPath, Bindings & autoArgs, Value & vIn
);

/**
 * Heuristic to find the filename and lineno or a nix value.
 */
std::pair<SourcePath, uint32_t> findPackageFilename(EvalState & state, Value & v, std::string what);

/**
 * Parses an attr path (as used in nix-build -A foo.bar.baz) into a list of tokens.
 *
 * Such an attr path is a dot-separated sequence of attribute names, which are possibly quoted.
 * No escaping is performed; attribute names containing double quotes are unrepresentable.
 */
std::vector<std::string> parseAttrPath(std::string_view const s);

/**
 * Converts an attr path from a list of strings into a string once more.
 * The result returned is an attr path and is *not necessarily valid nix syntax*.
 */
std::string unparseAttrPath(std::vector<std::string> const & attrPath);

}
