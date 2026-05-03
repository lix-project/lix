#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/store-api.hh"
#include <kj/async-io.h>

namespace nix::daemon {

void processLegacyConnection(
    AsyncIoRoot & aio, ref<Store> store, FdSource & from, FdSink & to, TrustedFlag trusted
);

kj::Promise<Result<void>>
processConnection(ref<Store> store, kj::AsyncIoStream & connection, TrustedFlag trusted);
}
