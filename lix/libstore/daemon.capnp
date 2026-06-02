@0xd8aa4d286ba6797b;

# IMPORTANT NOTICE
#
# these definitions are EXPERIMENTAL and come with NO stability guarantees

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::daemon");
$Cxx.allowCancellation;

using T = import "/lix/libutil/types.capnp";
using Log = import "/lix/libutil/logging.capnp";
using Libstore = import "/lix/libstore/types.capnp";

struct ProtocolDescription {
  id @0 :Text;
  description @1 :Text;
}

interface Bootstrap {
  supported @0 () -> (protocols :List(ProtocolDescription));
  request @1 (
    clientInfo :Text,
    protocol :Text,
  ) -> (result :Protocol);
}

interface Protocol {
  # TODO maybe add information or something
}

# Bootstrap protocol for the legacy protocol implementation
interface LegacyBoot extends(Protocol) $T.throws(T.v1Errors) {
  enum Trust {
    unknown @0;
    untrusted @1;
    trusted @2;
  }

  init @0 (
    logger :Log.LogStream,
    replyStream :LegacyStream,
  ) -> (result :InitResult);

  struct InitResult {
    protocol @3 :LegacyProtocol;
    requestStream @0 :LegacyStream;
    trust @1 :Trust;
    version @2 :Text;
  }
}

# The RPC'd version of the legacy protocol, with only minimal adjustments
interface LegacyProtocol $T.throws(T.v1Errors) {
  enum HashType {
    md5 @0;
    sha1 @1;
    sha256 @2;
    sha512 @3;
  }
  struct Hash {
    hash @0 :Data;
    hashType @1 :HashType;
  }
  enum ContentAddressMethod {
    textIngestion @0;
    flatFileIngestion @1;
    recursiveFileIngestion @2;
  }
  struct ContentAddress {
    method @0 :ContentAddressMethod;
    hash @1 :Hash;
  }
  struct UnkeyedValidPathInfo {
    deriver @0 :T.Option(Libstore.StorePath);
    narHash @1 :Hash;
    references @2 :List(Libstore.StorePath);
    registrationTime @3 :T.Time;
    narSize @4 :UInt64;
    ultimate @5 :Bool;
    sigs @6 :List(T.String);
    ca @7 :T.Option(ContentAddress);
  }
  struct ValidPathInfo {
    unkeyedValidPathInfo @0 :UnkeyedValidPathInfo;
    path @1 :Libstore.StorePath;
  }
  enum GCAction {
    returnLive @0;
    returnDead @1;
    deleteDead @2;
    deleteSpecific @3;
    tryDeleteSpecific @4;
  }

  interface AddToStoreStream {
    feed @0 (raw :Data) -> stream;
    finalize @1 () -> (result :ValidPathInfo);
  }

  addIndirectRoot @11 (path :T.String);
  addTempRoot @10 (path :Libstore.StorePath);
  addToStore @9 (
    name :T.String,
    contentAddressMethod :T.String,
    references :List(Libstore.StorePath),
    repair :Bool
  ) -> (result :AddToStoreStream);
  collectGarbage @13 (
    action :GCAction,
    pathsToDelete :List(Libstore.StorePath),
    ignoreLiveness :Bool,
    maxFreed :UInt64
  ) -> (
    paths :List(T.String),
    bytesFreed :UInt64
  );
  ensurePath @1 (path :Libstore.StorePath);
  findRoots @12 () -> (result :T.Map(Libstore.StorePath, List(T.String)));
  isValidPath @2 (path :Libstore.StorePath) -> (result :Bool);
  optimiseStore @0 ();
  queryValidPaths @3 (
    paths :List(Libstore.StorePath),
    substitute :Bool
  ) -> (
    result :List(Libstore.StorePath)
  );
  querySubstitutablePaths @4 (paths :List(Libstore.StorePath)) -> (result :List(Libstore.StorePath));
  queryReferrers @5 (path :Libstore.StorePath) -> (result :List(Libstore.StorePath));
  queryValidDerivers @6 (path :Libstore.StorePath) -> (result :List(Libstore.StorePath));
  queryDerivationOutputMap @7 (path :Libstore.StorePath) -> (result :T.Map(Text, Libstore.StorePath));
  queryPathFromHashPart @8 (hashPart :T.String) -> (result :T.Option(Libstore.StorePath));
  setOptions @14 (
    keepFailed :Bool,
    keepGoing :Bool,
    tryFallback :Bool,
    verbosity :T.Verbosity,
    maxBuildJobs :UInt32,
    maxSilentTime :UInt64,
    verboseBuild :Bool,
    buildCores :UInt32,
    useSubstitutes :Bool,
    settingsOverrides :T.Settings
  );
}

# Tunnel the un-RPC'd wire protocol over an RPC-style bytestream
interface LegacyStream $T.throws(T.v1Errors) {
  feed @0 (raw :Data) -> stream;
  # must be called before a new op is started, otherwise errors may get lost
  sync @1 ();
}
