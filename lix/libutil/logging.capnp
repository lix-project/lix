@0xff79d868797f140a;

# IMPORTANT NOTICE
#
# these definitions are EXPERIMENTAL and come with NO stability guarantees

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("nix::rpc::log");
$Cxx.allowCancellation;

using Types = import "/lix/libutil/types.capnp";

enum ActivityType {
  unknown @0;
  copyPath @1;
  fileTransfer @2;
  realise @3;
  copyPaths @4;
  builds @5;
  build @6;
  optimiseStore @7;
  verifyPaths @8;
  substitute @9;
  queryPathInfo @10;
  postBuildHook @11;
  buildWaiting @12;
}

enum ResultType {
  fileLinked @0;
  buildLogLine @1;
  untrustedPath @2;
  corruptedPath @3;
  setPhase @4;
  progress @5;
  setExpected @6;
  postBuildLogLine @7;
}

struct Event {
  struct Field {
    union {
      s @0 :Types.String;
      i @1 :UInt64;
    }
  }

  union {
    log :group {
      level @0 :Types.Verbosity;
      msg @1 :Types.String;
    }

    logEI :group {
      info @2 :Types.Error;
    }

    startActivity :group {
      level @3 :Types.Verbosity;
      id @4 :UInt64;
      type @5 :ActivityType;
      text @6 :Types.String;
      parent @7 :UInt64;
      fields @8 :List(Field);
    }

    stopActivity :group {
      id @9 :UInt64;
    }

    result :group {
      id @10 :UInt64;
      type @11 :ResultType;
      fields @12 :List(Field);
    }
  }
}

# NOTE: these streams do not return any result. loggers are expected to
# be infallible since once the logger fails the only thing we can still
# do is panic. reporting any status is impossible, and writing to other
# streams (like stdout/stderr) should be reserved for emergencies only.
#
# we also purposely do not model the c++ api to the loggers here. since
# the rpc definitions are still experimental and used only within *one*
# defined version of lix on a single machine we can get away with this.

interface LogStream {
  push @0 (e :Event) -> stream;

  # flush rpc streams and synchronize capnp flow control/error state
  synchronize @1 ();
}
