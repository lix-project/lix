#include "lix/libutil/serialise.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

std::optional<TrustedFlag> WorkerProto::Serialise<std::optional<TrustedFlag>>::read(const Store & store, WorkerProto::ReadConn conn)
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

WireFormatGenerator WorkerProto::Serialise<std::optional<TrustedFlag>>::write(const Store & store, WorkerProto::WriteConn conn, const std::optional<TrustedFlag> & optTrusted)
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


DerivedPath WorkerProto::Serialise<DerivedPath>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        return DerivedPath::parseLegacy(store, s);
    } else {
        return parsePathWithOutputs(store, s).toDerivedPath();
    }
}

WireFormatGenerator WorkerProto::Serialise<DerivedPath>::write(const Store & store, WorkerProto::WriteConn conn, const DerivedPath & req)
{
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        co_yield req.to_string_legacy(store);
    } else {
        auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(req);
        co_yield std::visit(overloaded {
            [&](const StorePathWithOutputs & s) -> std::string {
                return s.to_string(store);
            },
            [&](const StorePath & drvPath) -> std::string {
                throw Error("trying to request '%s', but daemon protocol %d.%d is too old (< 1.29) to request a derivation file",
                    store.printStorePath(drvPath),
                    GET_PROTOCOL_MAJOR(conn.version),
                    GET_PROTOCOL_MINOR(conn.version));
            },
            [&](std::monostate) -> std::string {
                throw Error("wanted to build a derivation that is itself a build product, but protocols do not support that. Try upgrading the Nix implementation on the other end of this connection");
            },
        }, sOrDrvPath);
    }
}


KeyedBuildResult WorkerProto::Serialise<KeyedBuildResult>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(store, conn);
    auto br = WorkerProto::Serialise<BuildResult>::read(store, conn);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

WireFormatGenerator WorkerProto::Serialise<KeyedBuildResult>::write(const Store & store, WorkerProto::WriteConn conn, const KeyedBuildResult & res)
{
    co_yield WorkerProto::write(store, conn, res.path);
    co_yield WorkerProto::write(store, conn, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto::Serialise<BuildResult>::read(const Store & store, WorkerProto::ReadConn conn)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(conn.from);
    conn.from >> res.errorMsg;
    if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
        conn.from
            >> res.timesBuilt
            >> res.isNonDeterministic
            >> res.startTime
            >> res.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
        auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            res.builtOutputs.insert_or_assign(
                std::move(output.outputName),
                std::move(realisation));
    }
    return res;
}

WireFormatGenerator WorkerProto::Serialise<BuildResult>::write(const Store & store, WorkerProto::WriteConn conn, const BuildResult & res)
{
    co_yield res.status;
    co_yield res.errorMsg;
    if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
        co_yield res.timesBuilt;
        co_yield res.isNonDeterministic;
        co_yield res.startTime;
        co_yield res.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
        DrvOutputs builtOutputs;
        for (auto & [output, realisation] : res.builtOutputs)
            builtOutputs.insert_or_assign(realisation.id, realisation);
        co_yield WorkerProto::write(store, conn, builtOutputs);
    }
}


ValidPathInfo WorkerProto::Serialise<ValidPathInfo>::read(const Store & store, ReadConn conn)
{
    auto path = WorkerProto::Serialise<StorePath>::read(store, conn);
    return ValidPathInfo {
        std::move(path),
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(store, conn),
    };
}

WireFormatGenerator WorkerProto::Serialise<ValidPathInfo>::write(const Store & store, WriteConn conn, const ValidPathInfo & pathInfo)
{
    co_yield WorkerProto::write(store, conn, pathInfo.path);
    co_yield WorkerProto::write(store, conn, static_cast<const UnkeyedValidPathInfo &>(pathInfo));
}


UnkeyedValidPathInfo WorkerProto::Serialise<UnkeyedValidPathInfo>::read(const Store & store, ReadConn conn)
{
    auto deriver = readString(conn.from);
    auto narHash = Hash::parseAny(readString(conn.from), HashType::SHA256);
    UnkeyedValidPathInfo info(narHash);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(store, conn);
    conn.from >> info.registrationTime >> info.narSize;

    conn.from >> info.ultimate;
    info.sigs = readStrings<StringSet>(conn.from);
    info.ca = ContentAddress::parseOpt(readString(conn.from));

    return info;
}

WireFormatGenerator WorkerProto::Serialise<UnkeyedValidPathInfo>::write(const Store & store, WriteConn conn, const UnkeyedValidPathInfo & pathInfo)
{
    co_yield (pathInfo.deriver ? store.printStorePath(*pathInfo.deriver) : "");
    co_yield pathInfo.narHash.to_string(Base::Base16, false);
    co_yield WorkerProto::write(store, conn, pathInfo.references);
    co_yield pathInfo.registrationTime;
    co_yield pathInfo.narSize;

    co_yield pathInfo.ultimate;
    co_yield pathInfo.sigs;
    co_yield renderContentAddress(pathInfo.ca);
}

}
