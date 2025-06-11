#include "lix/libutil/json.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

/* protocol-agnostic definitions */

bool CommonProto::Serialise<bool>::read(CommonProto::ReadConn conn)
{
    return readNum<uint64_t>(conn.from);
}

WireFormatGenerator CommonProto::Serialise<bool>::write(CommonProto::WriteConn conn, const bool & b)
{
    co_yield b;
}

unsigned CommonProto::Serialise<unsigned>::read(CommonProto::ReadConn conn)
{
    return readNum<unsigned>(conn.from);
}

WireFormatGenerator
CommonProto::Serialise<unsigned>::write(CommonProto::WriteConn conn, const unsigned & u)
{
    co_yield u;
}

uint64_t CommonProto::Serialise<uint64_t>::read(CommonProto::ReadConn conn)
{
    return readNum<uint64_t>(conn.from);
}

WireFormatGenerator
CommonProto::Serialise<uint64_t>::write(CommonProto::WriteConn conn, const uint64_t & u)
{
    co_yield u;
}

std::string CommonProto::Serialise<std::string>::read(CommonProto::ReadConn conn)
{
    return readString(conn.from);
}

WireFormatGenerator CommonProto::Serialise<std::string>::write(CommonProto::WriteConn conn, const std::string & str)
{
    co_yield str;
}


StorePath CommonProto::Serialise<StorePath>::read(CommonProto::ReadConn conn)
{
    return conn.store.parseStorePath(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<StorePath>::write(CommonProto::WriteConn conn, const StorePath & storePath)
{
    co_yield conn.store.printStorePath(storePath);
}


ContentAddress CommonProto::Serialise<ContentAddress>::read(CommonProto::ReadConn conn)
{
    return ContentAddress::parse(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<ContentAddress>::write(CommonProto::WriteConn conn, const ContentAddress & ca)
{
    co_yield renderContentAddress(ca);
}


Realisation CommonProto::Serialise<Realisation>::read(CommonProto::ReadConn conn)
{
    std::string rawInput = readString(conn.from);
    return Realisation::fromJSON(
        json::parse(rawInput),
        "remote-protocol"
    );
}

WireFormatGenerator CommonProto::Serialise<Realisation>::write(CommonProto::WriteConn conn, const Realisation & realisation)
{
    co_yield realisation.toJSON().dump();
}


DrvOutput CommonProto::Serialise<DrvOutput>::read(CommonProto::ReadConn conn)
{
    return DrvOutput::parse(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<DrvOutput>::write(CommonProto::WriteConn conn, const DrvOutput & drvOutput)
{
    co_yield drvOutput.to_string();
}


std::optional<StorePath> CommonProto::Serialise<std::optional<StorePath>>::read(CommonProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath> {} : conn.store.parseStorePath(s);
}

WireFormatGenerator CommonProto::Serialise<std::optional<StorePath>>::write(CommonProto::WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    return [](std::string s) -> WireFormatGenerator {
        co_yield s;
    }(storePathOpt ? conn.store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> CommonProto::Serialise<std::optional<ContentAddress>>::read(CommonProto::ReadConn conn)
{
    return ContentAddress::parseOpt(readString(conn.from));
}

WireFormatGenerator CommonProto::Serialise<std::optional<ContentAddress>>::write(CommonProto::WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    return [](std::string s) -> WireFormatGenerator {
        co_yield s;
    }(caOpt ? renderContentAddress(caOpt) : "");
}

}
