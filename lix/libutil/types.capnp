@0xd43cca581e0ebf82;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc");

enum Verbosity {
  error @0;
  warn @1;
  notice @2;
  info @3;
  talkative @4;
  chatty @5;
  debug @6;
  vomit @7;
}

struct Error {
  level @0 :Verbosity;
  message @1 :Data;
  traces @2 :List(Data);
}

struct Result(T) {
  union {
    good @0 :T;
    bad @1 :Error;
  }
}

# primitives can't be args to generics, so we need to specialize for primitive here.
struct ResultV {
  union {
    good @0 :Void;
    bad @1 :Error;
  }
}
