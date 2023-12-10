#pragma once
#include <nix/config.h>
#include <nix/shared.hh>
#include <nix/eval.hh>

#include "eval-args.hh"

void worker(nix::ref<nix::EvalState> state, nix::Bindings &autoArgs,
            nix::AutoCloseFD &to, nix::AutoCloseFD &from, MyArgs &args);
