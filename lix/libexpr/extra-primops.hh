#pragma once
///@file

#include "pos-idx.hh"

namespace nix {

class EvalState;
struct Value;

Value prim_addDrvOutputDependencies(EvalState & state, Value ** args);
Value prim_fetchTree(EvalState & state, Value ** args);
Value prim_fetchGit(EvalState & state, Value ** args);
Value prim_fetchMercurial(EvalState & state, Value ** args);
Value prim_fetchTarball(EvalState & state, Value ** args);
Value prim_fetchurl(EvalState & state, Value ** args);
Value prim_fromTOML(EvalState & state, Value ** args);
Value prim_appendContext(EvalState & state, Value ** args);
Value prim_getContext(EvalState & state, Value ** args);
Value prim_hasContext(EvalState & state, Value ** args);
Value prim_unsafeDiscardOutputDependency(EvalState & state, Value ** args);
Value prim_unsafeDiscardStringContext(EvalState & state, Value ** args);

namespace flake {

Value prim_flakeRefToString(EvalState & state, Value ** args);
Value prim_getFlake(EvalState & state, Value ** args);
Value prim_parseFlakeRef(EvalState & state, Value ** args);
}

}
