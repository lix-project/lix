#include "lix/libutil/serialise.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh"
#include "lix/libstore/derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-agnostic definitions */

std::string CommonProto::Serialise<std::string>::read(const Store & store, CommonProto::ReadConn conn)
{
    return readString(conn.from);
}

WireFormatGenerator CommonProto::Serialise<std::string>::write(const Store & store, CommonProto::WriteConn conn, const std::string & str)
{
    co_yield str;
}


StorePath CommonProto::Serialise<StorePath>::read(const Store & store, CommonProto::ReadConn conn)
{
    return store.parseStorePath(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<StorePath>::write(const Store & store, CommonProto::WriteConn conn, const StorePath & storePath)
{
    co_yield store.printStorePath(storePath);
}


ContentAddress CommonProto::Serialise<ContentAddress>::read(const Store & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parse(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<ContentAddress>::write(const Store & store, CommonProto::WriteConn conn, const ContentAddress & ca)
{
    co_yield renderContentAddress(ca);
}


Realisation CommonProto::Serialise<Realisation>::read(const Store & store, CommonProto::ReadConn conn)
{
    std::string rawInput = readString(conn.from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

WireFormatGenerator CommonProto::Serialise<Realisation>::write(const Store & store, CommonProto::WriteConn conn, const Realisation & realisation)
{
    co_yield realisation.toJSON().dump();
}


DrvOutput CommonProto::Serialise<DrvOutput>::read(const Store & store, CommonProto::ReadConn conn)
{
    return DrvOutput::parse(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<DrvOutput>::write(const Store & store, CommonProto::WriteConn conn, const DrvOutput & drvOutput)
{
    co_yield drvOutput.to_string();
}


std::optional<StorePath> CommonProto::Serialise<std::optional<StorePath>>::read(const Store & store, CommonProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

WireFormatGenerator CommonProto::Serialise<std::optional<StorePath>>::write(const Store & store, CommonProto::WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    return [](std::string s) -> WireFormatGenerator {
        co_yield s;
    }(storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> CommonProto::Serialise<std::optional<ContentAddress>>::read(const Store & store, CommonProto::ReadConn conn)
{
    return ContentAddress::parseOpt(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<std::optional<ContentAddress>>::write(const Store & store, CommonProto::WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    return [](std::string s) -> WireFormatGenerator {
        co_yield s;
    }(caOpt ? renderContentAddress(caOpt) : "");
}

}
