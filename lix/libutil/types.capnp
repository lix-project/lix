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

# NOTE ON ERRORS
#
# interfaces in this system may throw errors in an encoded form. this encoded form is not
# itself meant to be read directly, but to be decoded and rethrown in a better error type
# than the (very limited) kj exceptions. transporting errors in result types inhibits all
# pipelining optimizations capnp can do for us (including early aborts in streaming types
# like encapsulated byte streams, or loggers). all interfaces should be annotated with an
# appropriate marker to document how their errors are transported. these annotations have
# NO IMPACT ON CODE GENERATION, error wrapping must still be done in each implementation.

struct ErrorEncoding {
  header @0 :Text;
  trailer @1 :Text;
}

const v1Errors :ErrorEncoding = (
  header = "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:",
  trailer = ":v1}",
);

# annotations on interfaces also apply to methods and recursively to all child interfaces
annotation throws(interface, method) :ErrorEncoding;

struct Error {
  level @0 :Verbosity;
  message @1 :Data;
  traces @2 :List(Data);
}

struct Option(T) {
  union {
    # make sure uninitialized options deserialize to none for some added safety
    none @0 :Void;
    some @1 :T;
  }
}

struct OptionInt64 {
  union {
    none @0 :Void;
    some @1 :Int64;
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
