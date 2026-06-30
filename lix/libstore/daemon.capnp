@0xd8aa4d286ba6797b;

# IMPORTANT NOTICE
#
# these definitions are EXPERIMENTAL and come with NO stability guarantees

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::daemon");
$Cxx.allowCancellation;

using T = import "/lix/libutil/types.capnp";
using Log = import "/lix/libutil/logging.capnp";

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

# legacy boot protocol. EXPLICITLY UNSTABLE, this id will change frequently and without notice.
# every change to the experimental tunneling protocol may also change this protocol identifier.
const unstableLegacyTunneled :Text = "lix/legacy/ba3153c5-4153-4d66-91ec-a258c02e9a3c";

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
    requestStream @0 :LegacyStream;
    trust @1 :Trust;
    version @2 :Text;
  }
}

interface LegacyStream $T.throws(T.v1Errors) {
  feed @0 (raw :Data) -> stream;
  # must be called before a new op is started, otherwise errors may get lost
  sync @1 ();
}
