@0x8f8131bf93ce599c;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc");

struct StorePath {
  raw @0 :Data;
}
