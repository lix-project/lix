@0xa7f090bdc7816f8f;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::build");

struct Request {
  builder @0 :Data;
  args @1 :List(Data);
  environment @2 :List(Data);
}
