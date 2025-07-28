#include "lix/libutil/serialise.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/path-info.hh"
#include <cstdint>
#include <ctime>
#include <optional>

namespace nix {

/* protocol-specific definitions */

std::optional<TrustedFlag> WorkerProto::Serialise<std::optional<TrustedFlag>>::read(WorkerProto::ReadConn conn)
{
    auto temp = readNum<uint8_t>(conn.from);
    switch (temp) {
        case 0:
            return std::nullopt;
        case 1:
            return { Trusted };
        case 2:
            return { NotTrusted };
        default:
            throw Error("Invalid trusted status from remote");
    }
}

WireFormatGenerator WorkerProto::Serialise<std::optional<TrustedFlag>>::write(WorkerProto::WriteConn conn, const std::optional<TrustedFlag> & optTrusted)
{
    if (!optTrusted)
        co_yield (uint8_t)0;
    else {
        switch (*optTrusted) {
        case Trusted:
            co_yield (uint8_t)1;
            break;
        case NotTrusted:
            co_yield (uint8_t)2;
            break;
        default:
            assert(false);
        };
    }
}


DerivedPath WorkerProto::Serialise<DerivedPath>::read(WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return DerivedPath::parseLegacy(conn.store, s);
}

WireFormatGenerator WorkerProto::Serialise<DerivedPath>::write(WorkerProto::WriteConn conn, const DerivedPath & req)
{
    co_yield req.to_string_legacy(conn.store);
}


KeyedBuildResult WorkerProto::Serialise<KeyedBuildResult>::read(WorkerProto::ReadConn conn)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(conn);
    auto br = WorkerProto::Serialise<BuildResult>::read(conn);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

WireFormatGenerator WorkerProto::Serialise<KeyedBuildResult>::write(WorkerProto::WriteConn conn, const KeyedBuildResult & res)
{
    co_yield WorkerProto::write(conn, res.path);
    co_yield WorkerProto::write(conn, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto::Serialise<BuildResult>::read(WorkerProto::ReadConn conn)
{
    BuildResult res;
    res.status = (BuildResult::Status) readNum<unsigned>(conn.from);
    res.errorMsg = readString(conn.from);
    res.timesBuilt = readNum<unsigned>(conn.from);
    res.isNonDeterministic = readBool(conn.from);
    res.startTime = readNum<time_t>(conn.from);
    res.stopTime = readNum<time_t>(conn.from);
    auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(conn);
    for (auto && [output, realisation] : builtOutputs)
        res.builtOutputs.insert_or_assign(
            std::move(output.outputName),
            std::move(realisation));
    return res;
}

WireFormatGenerator WorkerProto::Serialise<BuildResult>::write(WorkerProto::WriteConn conn, const BuildResult & res)
{
    co_yield res.status;
    co_yield res.errorMsg;
    co_yield res.timesBuilt;
    co_yield res.isNonDeterministic;
    co_yield res.startTime;
    co_yield res.stopTime;
    DrvOutputs builtOutputs;
    for (auto & [output, realisation] : res.builtOutputs)
        builtOutputs.insert_or_assign(realisation.id, realisation);
    co_yield WorkerProto::write(conn, builtOutputs);
}


ValidPathInfo WorkerProto::Serialise<ValidPathInfo>::read(ReadConn conn)
{
    auto path = WorkerProto::Serialise<StorePath>::read(conn);
    return ValidPathInfo {
        std::move(path),
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(conn),
    };
}

WireFormatGenerator WorkerProto::Serialise<ValidPathInfo>::write(WriteConn conn, const ValidPathInfo & pathInfo)
{
    co_yield WorkerProto::write(conn, pathInfo.path);
    co_yield WorkerProto::write(conn, static_cast<const UnkeyedValidPathInfo &>(pathInfo));
}


UnkeyedValidPathInfo WorkerProto::Serialise<UnkeyedValidPathInfo>::read(ReadConn conn)
{
    auto deriver = readString(conn.from);
    auto narHash = Hash::parseAny(readString(conn.from), HashType::SHA256);
    UnkeyedValidPathInfo info(narHash);
    if (deriver != "") info.deriver = conn.store.parseStorePath(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(conn);
    info.registrationTime = readNum<time_t>(conn.from);
    info.narSize = readNum<uint64_t>(conn.from);

    info.ultimate = readBool(conn.from);
    info.sigs = readStrings<StringSet>(conn.from);
    info.ca = ContentAddress::parseOpt(readString(conn.from));

    return info;
}

WireFormatGenerator WorkerProto::Serialise<UnkeyedValidPathInfo>::write(WriteConn conn, const UnkeyedValidPathInfo & pathInfo)
{
    co_yield (pathInfo.deriver ? conn.store.printStorePath(*pathInfo.deriver) : "");
    co_yield pathInfo.narHash.to_string(Base::Base16, false);
    co_yield WorkerProto::write(conn, pathInfo.references);
    co_yield pathInfo.registrationTime;
    co_yield pathInfo.narSize;

    co_yield pathInfo.ultimate;
    co_yield pathInfo.sigs;
    co_yield renderContentAddress(pathInfo.ca);
}

std::optional<UnkeyedValidPathInfo>
WorkerProto::Serialise<std::optional<UnkeyedValidPathInfo>>::read(ReadConn conn)
{
    bool valid = readBool(conn.from);
    if (valid) {
        return WorkerProto::Serialise<UnkeyedValidPathInfo>::read(conn);
    } else {
        return std::nullopt;
    }
}

WireFormatGenerator WorkerProto::Serialise<std::optional<UnkeyedValidPathInfo>>::write(
    WriteConn conn, const std::optional<UnkeyedValidPathInfo> & pathInfo
)
{
    co_yield pathInfo.has_value();
    if (pathInfo.has_value()) {
        co_yield WorkerProto::write(conn, *pathInfo);
    }
}

SubstitutablePathInfo WorkerProto::Serialise<SubstitutablePathInfo>::read(ReadConn conn)
{
    SubstitutablePathInfo info;
    auto deriver = readString(conn.from);
    if (deriver != "")
        info.deriver = conn.store.parseStorePath(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(conn);
    info.downloadSize = readNum<uint64_t>(conn.from);
    info.narSize = readNum<uint64_t>(conn.from);
    return info;
}

WireFormatGenerator WorkerProto::Serialise<SubstitutablePathInfo>::write(WriteConn conn, const SubstitutablePathInfo & info)
{
    co_yield (info.deriver ? conn.store.printStorePath(*info.deriver) : "");
    co_yield WorkerProto::write(conn, info.references);
    co_yield info.downloadSize;
    co_yield info.narSize;
}

}
