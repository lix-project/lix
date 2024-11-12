#include "lix/libexpr/eval.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return path;
}

}
