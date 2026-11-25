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

  init @0 (logger :Log.LogStream) -> (protocol :LegacyProtocol);
}

# legacy rpc protocol data structures. UNSTABLE.
struct DerivedPathOpaque {
  path @0 :Libstore.StorePath;
}
struct DerivedPathBuilt {
  drvPath @0 :DerivedPathOpaque;
  outputs :union {
    all @1 :Void;
    names @2 :List(T.String);
  }
}
struct DerivedPath {
  raw :union {
    opaque @0 :DerivedPathOpaque;
    built @1 :DerivedPathBuilt;
  }
}
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

# The RPC'd version of LogStore, very close to the legacy protocol. we will change this.
interface LegacyLogStore $T.throws(T.v1Errors) {
  interface AddLogStream {
    feed @0 (raw :Data) -> stream;
    finalize @1 ();
  }

  addBuildLog @0 (path :Libstore.StorePath) -> (stream :AddLogStream);
  getBuildLogExact @1 (path :Libstore.StorePath) -> (log :T.Option(Data));
}

# The RPC'd fragment of build-related ops of the legacy protocol. we will change this.
interface LegacyBuildStore $T.throws(T.v1Errors) {
  enum BuildMode {
    bmNormal @0;
    bmRepair @1;
    bmCheck @2;
  }
  struct BuildResult {
    enum Status {
        built @0;
        substituted @1;
        alreadyValid @2;
        permanentFailure @3;
        inputRejected @4;
        outputRejected @5;
        transientFailure @6; # possibly transient
        timedOut @7;
        miscFailure @8;
        dependencyFailed @9;
        logLimitExceeded @10;
        notDeterministic @11;
        resolvesToAlreadyValid @12;
        noSubstituters @13;
    }

    status @0 :Status;
    errorMsg @1 :T.String;
    timesBuilt @2 :UInt32;
    isNonDeterministic @3 :Bool;
    buildOutputs @4 :T.Map(T.String, Realisation);
    startTime @5 :Int64;
    stopTime @6 :Int64;
    cpuUser @7 :T.OptionInt64;
    cpuSystem @8 :T.OptionInt64;
  }
  struct DrvOutput {
    drvHash @0 :Hash;
    outputName @1 :T.String;
  }
  struct KeyedBuildResult {
    result @0 :BuildResult;
    path @1 :DerivedPath;
  }
  struct Realisation {
    id @0 :DrvOutput;
    outPath @1 :Libstore.StorePath;
    signatures @2 :List(T.String);
    dependentRealisations @3 :T.Map(DrvOutput, Libstore.StorePath);
  }

  # TODO remove in favor of only buildPathsWithResult
  buildPaths @0 (mode :BuildMode, paths :List(DerivedPath));
  buildPathsWithResult @1 (mode :BuildMode, paths :List(DerivedPath)) -> (result :List(KeyedBuildResult));
}

# The RPC'd version of the legacy protocol, with only minimal adjustments
interface LegacyProtocol extends (LegacyLogStore, LegacyBuildStore) $T.throws(T.v1Errors) {
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
  struct QueryMissingResult {
    willBuild @0 :List(Libstore.StorePath);
    willSubstitute @1 :List(Libstore.StorePath);
    unknown @2 :List(Libstore.StorePath);
    downloadSize @3 :UInt64;
    narSize @4 :UInt64;
  }

  interface AddToStoreStream {
    feed @0 (raw :Data) -> stream;
    finalize @1 () -> (result :ValidPathInfo);
  }
  interface Stream {
    feed @0 (raw :Data) -> stream;
    finalize @1 ();
  }

  addIndirectRoot @10 (path :T.String);
  addSignatures @16 (path :Libstore.StorePath, signatures :List(T.String));
  addTempRoot @9 (path :Libstore.StorePath);
  addCAToStore @8 (
    name :T.String,
    contentAddressMethod :ContentAddressMethod,
    hashType :HashType,
    references :List(Libstore.StorePath),
    repair :Bool
  ) -> (result :AddToStoreStream);
  addToStoreNar @17 (
    info: ValidPathInfo,
    repair :Bool,
    dontCheckSigs :Bool
  ) -> (
    result :Stream
  );
  collectGarbage @12 (
    action :GCAction,
    pathsToDelete :List(Libstore.StorePath),
    ignoreLiveness :Bool,
    maxFreed :UInt64
  ) -> (
    paths :List(T.String),
    bytesFreed :UInt64
  );
  findRoots @11 () -> (result :T.Map(Libstore.StorePath, List(T.String)));
  isValidPath @1 (path :Libstore.StorePath) -> (result :Bool);
  narFromPath @19 (path :Libstore.StorePath, into :Stream);
  optimiseStore @0 ();
  queryAllValidPaths @20 () -> (result :List(Libstore.StorePath));
  queryValidPaths @2 (paths :List(Libstore.StorePath)) -> (result :List(Libstore.StorePath));
  querySubstitutablePaths @3 (paths :List(Libstore.StorePath)) -> (result :List(Libstore.StorePath));
  queryReferrers @4 (path :Libstore.StorePath) -> (result :List(Libstore.StorePath));
  queryValidDerivers @5 (path :Libstore.StorePath) -> (result :List(Libstore.StorePath));
  queryDerivationOutputMap @6 (path :Libstore.StorePath) -> (result :T.Map(Text, Libstore.StorePath));
  queryMissing @18 (targets :List(DerivedPath)) -> (result :QueryMissingResult);
  queryPathFromHashPart @7 (hashPart :T.String) -> (result :T.Option(Libstore.StorePath));
  queryPathInfo @14 (path :Libstore.StorePath) -> (result :T.Option(ValidPathInfo));
  setOptions @13 (verbosity :T.Verbosity, settingsOverrides :T.Settings, clientPreferences :T.Settings);
  verifyStore @15 (checkContents :Bool, repair :Bool) -> (result :Bool);

  getTrust @21 () -> (trust :LegacyBoot.Trust);
  getVersion @22 () -> (version :Text);
}
