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

  struct LinuxPlatform {
    struct BoundPath {
      source @0 :Data;
      target @1 :Data;
      optional @2 :Bool;
    }

    struct Sandbox {
      paths @0 :List(BoundPath);
      privateNetwork @1 :Bool;
      chrootRootDir @2 :Data;
      storeDir @3 :Data;
      wantsKvm @4 :Bool;
      sandboxShmFlags @5 :Text;
      useUidRange @6 :Bool;
      uid @7 :UInt32;
      gid @8 :UInt32;
      waitForInterface @9 :Text;
    }

    seccompFilters @0 :Data;
    sandbox @1 :Sandbox;
    platform @2 :Data;
    parentPid @3 :Int32;
  }

  struct DarwinPlatform {
    allowLocalNetworking @0 :Bool;
    sandboxProfile @1 :Text;
    platform @2 :Data;
    tempDir @3 :Text;
    globalTempDir @4 :Text;
  }

  builder @0 :Data;
  args @1 :List(Data);
  environment @2 :List(Data);
  workingDir @3 :Data;
  enableCoreDumps @4 :Bool;
  credentials @5 :Credentials;
  debug @8 :Bool;

  platform :union {
    linux @6 :LinuxPlatform;
    darwin @7 :DarwinPlatform;
  }
}
