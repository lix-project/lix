@0xfa3817f907240eb2;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::build_remote");
$Cxx.allowCancellation;

using Types = import "/lix/libutil/types.capnp";
using StoreTypes = import "/lix/libstore/types.capnp";

interface HookInstance {
  interface BuildLogger {
    # only used for fd passing
  }

  interface AcceptedBuild {
    run @0 (
      inputs :List(StoreTypes.StorePath), # actual a set
      wantedOutputs :List(Data), # actually StringSet
    ) -> (result :Types.ResultV);
  }

  struct BuildResponse {
    union {
      accept :group {
        machine @0 :AcceptedBuild;
        machineName @1 :Data;
      }
      postpone @2 :Void;
      decline @3 :Void;
      declinePermanently @4 :Void;
    }
  }

  build @0 (
    amWilling :Bool,
    neededSystem :Data,
    drvPath :StoreTypes.StorePath,
    requiredFeatures :List(Data),
    buildLogger :BuildLogger,
  ) -> (result :Types.Result(BuildResponse));
}
