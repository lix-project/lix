#include "lix/libutil/serialise.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/serve-protocol.hh"
#include "lix/libstore/serve-protocol-impl.hh"
#include "lix/libstore/path-info.hh"
#include <cstdint>

namespace nix {

/* protocol-specific definitions */

BuildResult ServeProto::Serialise<BuildResult>::read(ServeProto::ReadConn conn)
{
    BuildResult status;
    status.status = (BuildResult::Status) readNum<unsigned>(conn.from);
    status.errorMsg = readString(conn.from);

    if (GET_PROTOCOL_MINOR(conn.version) >= 3) {
        status.timesBuilt = readNum<unsigned>(conn.from);
        status.isNonDeterministic = readBool(conn.from);
        status.startTime = readNum<time_t>(conn.from);
        status.stopTime = readNum<time_t>(conn.from);
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        auto builtOutputs = ServeProto::Serialise<DrvOutputs>::read(conn);
        for (auto && [output, realisation] : builtOutputs)
            status.builtOutputs.insert_or_assign(
                std::move(output.outputName),
                std::move(realisation));
    }
    return status;
}

WireFormatGenerator ServeProto::Serialise<BuildResult>::write(ServeProto::WriteConn conn, const BuildResult & status)
{
    co_yield status.status;
    co_yield status.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 3) {
        co_yield status.timesBuilt;
        co_yield status.isNonDeterministic;
        co_yield status.startTime;
        co_yield status.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        DrvOutputs builtOutputs;
        for (auto & [output, realisation] : status.builtOutputs)
            builtOutputs.insert_or_assign(realisation.id, realisation);
        co_yield ServeProto::write(conn, builtOutputs);
    }
}


UnkeyedValidPathInfo ServeProto::Serialise<UnkeyedValidPathInfo>::read(ReadConn conn)
{
    /* Hash should be set below unless very old `nix-store --serve`.
       Caller should assert that it did set it. */
    UnkeyedValidPathInfo info { Hash::dummy };

    auto deriver = readString(conn.from);
    if (deriver != "")
        info.deriver = conn.store.parseStorePath(deriver);
    info.references = ServeProto::Serialise<StorePathSet>::read(conn);

    readNum<uint64_t>(conn.from); // download size, unused
    info.narSize = readNum<uint64_t>(conn.from);

    if (GET_PROTOCOL_MINOR(conn.version) >= 4) {
        auto s = readString(conn.from);
        if (!s.empty())
            info.narHash = Hash::parseAnyPrefixed(s);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
        info.sigs = readStrings<StringSet>(conn.from);
    }

    return info;
}

WireFormatGenerator ServeProto::Serialise<UnkeyedValidPathInfo>::write(WriteConn conn, const UnkeyedValidPathInfo & info)
{
    co_yield (info.deriver ? conn.store.printStorePath(*info.deriver) : "");

    co_yield ServeProto::write(conn, info.references);
    // !!! Maybe we want compression?
    co_yield info.narSize; // downloadSize, lie a little
    co_yield info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 4) {
        co_yield info.narHash.to_string(Base::Base32, true);
        co_yield renderContentAddress(info.ca);
        co_yield info.sigs;
    }
}

}
