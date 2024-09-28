#pragma once
///@file

#include "pos-idx.hh"

namespace nix {

class EvalState;
struct Value;

void prim_addDrvOutputDependencies(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_fetchClosure(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_fetchGit(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_fetchTarball(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_fetchurl(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_fromTOML(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_getContext(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_hasContext(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_unsafeDiscardOutputDependency(EvalState & state, const PosIdx pos, Value * * args, Value & v);

namespace flake {

void prim_flakeRefToString(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_getFlake(EvalState & state, const PosIdx pos, Value * * args, Value & v);
void prim_parseFlakeRef(EvalState & state, const PosIdx pos, Value * * args, Value & v);

}

}
