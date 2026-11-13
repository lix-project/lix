@0xfa3817f907240eb2;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::build_remote");
$Cxx.allowCancellation;

using Types = import "/lix/libutil/types.capnp";
using Log = import "/lix/libutil/logging.capnp";
using StoreTypes = import "/lix/libstore/types.capnp";

interface HookInstance $Types.throws(Types.v1Errors) {
  interface AcceptedBuild {
    run @0 (
      logger :Log.LogStream,
      lockDir :Data,
      inputs :List(StoreTypes.StorePath), # actual a set
      wantedOutputs :List(Data), # actually StringSet
    );
  }

  struct BuildResponse {
    union {
      accept :group {
        machine @0 :AcceptedBuild;
      }
      storeConnectionFailed @1 :Data;
    }
  }

  struct Machine {
    name @0 :Data;
    uri @1 :Data;
    systemTypes @2 :List(Data);
    sshKey @3 :Data;
    maxJobs @4 :UInt32;
    speedFactor @5 :Float32;
    supportedFeatures @6 :List(Data);
    mandatoryFeatures @7 :List(Data);
    sshPublicHostKey @8 :Data;
    # enabled is implied true
  }

  init @0 (
    logger :Log.LogStream,
    settings :Types.Settings,
    machine :Machine,
    drvPath :StoreTypes.StorePath,
  ) -> (result :BuildResponse);
}
