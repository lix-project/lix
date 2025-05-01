#pragma once
///@file

#include "pos-idx.hh"

namespace nix {

class EvalState;
struct Value;

void prim_addDrvOutputDependencies(EvalState & state, Value * * args, Value & v);
void prim_fetchClosure(EvalState & state, Value * * args, Value & v);
void prim_fetchTree(EvalState & state, Value * * args, Value & v);
void prim_fetchGit(EvalState & state, Value * * args, Value & v);
void prim_fetchTarball(EvalState & state, Value * * args, Value & v);
void prim_fetchurl(EvalState & state, Value * * args, Value & v);
void prim_fromTOML(EvalState & state, Value * * args, Value & v);
void prim_getContext(EvalState & state, Value * * args, Value & v);
void prim_hasContext(EvalState & state, Value * * args, Value & v);
void prim_unsafeDiscardOutputDependency(EvalState & state, Value * * args, Value & v);

namespace flake {

void prim_flakeRefToString(EvalState & state, Value * * args, Value & v);
void prim_getFlake(EvalState & state, Value * * args, Value & v);
void prim_parseFlakeRef(EvalState & state, Value * * args, Value & v);

}

}
