@0xd43cca581e0ebf82;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc");

# many of our strings must be nul-safe :(
using String = Data;

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

struct Settings {
  struct Setting {
    name @0 :Data;
    value @1 :Data;
  }

  # actually a map, but will treat it as a last-value-wins list of pairs for now.
  map @0 :List(Setting);
}
