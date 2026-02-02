@0xa7f090bdc7816f8f;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::build");

struct Request {
  struct Credentials {
    uid @0 :UInt32;
    gid @1 :UInt32;
    uidCount @2 :UInt32;
    supplementaryGroups @3 :List(UInt32);
  }

  builder @0 :Data;
  args @1 :List(Data);
  environment @2 :List(Data);
  workingDir @3 :Data;
  enableCoreDumps @4 :Bool;
  credentials @5 :Credentials;
}
