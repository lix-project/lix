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
  optimiseStore @0 ();
}

# Tunnel the un-RPC'd wire protocol over an RPC-style bytestream
interface LegacyStream $T.throws(T.v1Errors) {
  feed @0 (raw :Data) -> stream;
  # must be called before a new op is started, otherwise errors may get lost
  sync @1 ();
}
