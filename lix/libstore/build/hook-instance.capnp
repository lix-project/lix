@0xfa3817f907240eb2;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::build_remote");
$Cxx.allowCancellation;

using Types = import "/lix/libutil/types.capnp";
using Log = import "/lix/libutil/logging.capnp";
using StoreTypes = import "/lix/libstore/types.capnp";

interface HookInstance {
  interface AcceptedBuild {
    run @0 (
      logger :Log.LogStream,
      inputs :List(StoreTypes.StorePath), # actual a set
      wantedOutputs :List(Data), # actually StringSet
      description :Text, # root activity description for this build
    ) -> (result :Types.ResultV);
  }

  struct BuildResponse {
    union {
      accept :group {
        machine @0 :AcceptedBuild;
      }
      postpone @1 :Void;
      decline @2 :Void;
      declinePermanently @3 :Void;
    }
  }

  init @0 (
    logger :Log.LogStream,
    settings :Types.Settings,
  ) -> (result :Types.ResultV);
  build @1 (
    amWilling :Bool,
    neededSystem :Data,
    drvPath :StoreTypes.StorePath,
    requiredFeatures :List(Data),
  ) -> (result :Types.Result(BuildResponse));
}
