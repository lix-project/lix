#include "serialise.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

BuildResult ServeProto::Serialise<BuildResult>::read(const Store & store, ServeProto::ReadConn conn)
{
    BuildResult status;
    status.status = (BuildResult::Status) readInt(conn.from);
    conn.from >> status.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.from
            >> status.timesBuilt
            >> status.isNonDeterministic
            >> status.startTime
            >> status.stopTime;
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        auto builtOutputs = ServeProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            status.builtOutputs.insert_or_assign(
                std::move(output.outputName),
                std::move(realisation));
    }
    return status;
}

WireFormatGenerator ServeProto::Serialise<BuildResult>::write(const Store & store, ServeProto::WriteConn conn, const BuildResult & status)
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
        co_yield ServeProto::write(store, conn, builtOutputs);
    }
}


UnkeyedValidPathInfo ServeProto::Serialise<UnkeyedValidPathInfo>::read(const Store & store, ReadConn conn)
{
    /* Hash should be set below unless very old `nix-store --serve`.
       Caller should assert that it did set it. */
    UnkeyedValidPathInfo info { Hash::dummy };

    auto deriver = readString(conn.from);
    if (deriver != "")
        info.deriver = store.parseStorePath(deriver);
    info.references = ServeProto::Serialise<StorePathSet>::read(store, conn);

    readLongLong(conn.from); // download size, unused
    info.narSize = readLongLong(conn.from);

    if (GET_PROTOCOL_MINOR(conn.version) >= 4) {
        auto s = readString(conn.from);
        if (!s.empty())
            info.narHash = Hash::parseAnyPrefixed(s);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
        info.sigs = readStrings<StringSet>(conn.from);
    }

    return info;
}

WireFormatGenerator ServeProto::Serialise<UnkeyedValidPathInfo>::write(const Store & store, WriteConn conn, const UnkeyedValidPathInfo & info)
{
    co_yield (info.deriver ? store.printStorePath(*info.deriver) : "");

    co_yield ServeProto::write(store, conn, info.references);
    // !!! Maybe we want compression?
    co_yield info.narSize; // downloadSize, lie a little
    co_yield info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 4) {
        co_yield info.narHash.to_string(Base32, true);
        co_yield renderContentAddress(info.ca);
        co_yield info.sigs;
    }
}

}
