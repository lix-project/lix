#pragma once
#include <lix/libmain/shared.hh>
#include <lix/libexpr/eval.hh>

#include "eval-args.hh"

class MyArgs;

namespace nix {
class AutoCloseFD;
class Bindings;
class EvalState;
template <typename T> class ref;
}  // namespace nix

void worker(nix::ref<nix::EvalState> state, nix::Bindings &autoArgs,
            nix::AutoCloseFD &to, nix::AutoCloseFD &from, MyArgs &args);
