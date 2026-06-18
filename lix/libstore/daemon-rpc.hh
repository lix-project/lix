#pragma once
///@file RPC helper functions for the daemon protocol

#include "lix/libstore/content-address.hh"
#include "lix/libstore/daemon.capnp.h"
#include "lix/libstore/path-info.hh"
#include "lix/libstore/types-rpc.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/types-rpc.hh"

namespace nix::rpc {
namespace daemon {
inline nix::HashType from(LegacyProtocol::HashType ht, auto &&...)
{
    switch (ht) {
    case LegacyProtocol::HashType::MD5:
        return HashType::MD5;
    case LegacyProtocol::HashType::SHA1:
        return HashType::SHA1;
    case LegacyProtocol::HashType::SHA256:
        return HashType::SHA256;
    case LegacyProtocol::HashType::SHA512:
        return HashType::SHA512;
    }
    throw nix::Error("invalid HashType received over RPC: %d", uint16_t(ht));
}

inline LegacyProtocol::HashType to(nix::HashType ht, auto &&...)
{
    switch (ht) {
    case nix::HashType::MD5:
        return LegacyProtocol::HashType::MD5;
    case nix::HashType::SHA1:
        return LegacyProtocol::HashType::SHA1;
    case nix::HashType::SHA256:
        return LegacyProtocol::HashType::SHA256;
    case nix::HashType::SHA512:
        return LegacyProtocol::HashType::SHA512;
    }
    abort(); // unreachable
}

inline nix::ContentAddressMethod from(LegacyProtocol::ContentAddressMethod cam, auto &&...)
{
    switch (cam) {
    case LegacyProtocol::ContentAddressMethod::TEXT_INGESTION:
        return {TextIngestionMethod{}};
    case LegacyProtocol::ContentAddressMethod::FLAT_FILE_INGESTION:
        return {FileIngestionMethod::Flat};
    case LegacyProtocol::ContentAddressMethod::RECURSIVE_FILE_INGESTION:
        return {FileIngestionMethod::Recursive};
    }
    throw nix::Error("invalid ContentAddressMethod received over RPC: %d", uint16_t(cam));
}

inline LegacyProtocol::ContentAddressMethod to(const nix::ContentAddressMethod & cam, auto &&...)
{
    return std::visit(
        overloaded{
            [](const TextIngestionMethod &) { return LegacyProtocol::ContentAddressMethod::TEXT_INGESTION; },
            [](const FileIngestionMethod & fim) {
                return fim == FileIngestionMethod::Recursive
                    ? LegacyProtocol::ContentAddressMethod::RECURSIVE_FILE_INGESTION
                    : LegacyProtocol::ContentAddressMethod::FLAT_FILE_INGESTION;
            },
        },
        cam.raw
    );
}

inline nix::Hash from(const LegacyProtocol::Hash::Reader & h, auto &&... args)
{
    auto type = from(h.getHashType(), args...);
    auto bytes = h.getHash();
    if (bytes.size() >= nix::Hash::maxHashSize) {
        throw nix::Error("oversize hash rejected");
    }
    nix::Hash result(bytes.size(), type);
    std::memcpy(result.hash, bytes.begin(), bytes.size());
    return result;
}
}

template<>
struct Fill<daemon::LegacyProtocol::Hash, Hash>
{
    static void fill(daemon::LegacyProtocol::Hash::Builder hb, const Hash & h, auto &&... args)
    {
        hb.setHashType(daemon::to(h.type, args...));
        hb.setHash(kj::ArrayPtr<const capnp::byte>{h.hash, h.hashSize});
    }
};

namespace daemon {
inline nix::ContentAddress from(const LegacyProtocol::ContentAddress::Reader & ca, auto &&... args)
{
    return ContentAddress{
        .method = from(ca.getMethod(), args...),
        .hash = from(ca.getHash(), args...),
    };
}
}

template<>
struct Fill<daemon::LegacyProtocol::ContentAddress, ContentAddress>
{
    static void
    fill(daemon::LegacyProtocol::ContentAddress::Builder cb, const ContentAddress & ca, auto &&... args)
    {
        cb.setMethod(daemon::to(ca.method, args...));
        LIX_RPC_FILL_STRUCT(cb, initHash, ca.hash, args...);
    }
};

namespace daemon {
inline UnkeyedValidPathInfo from(const LegacyProtocol::UnkeyedValidPathInfo::Reader & r, auto &&... args)
{
    UnkeyedValidPathInfo info{from(r.getNarHash(), args...)};
    info.deriver = rpc::from(r.getDeriver(), args...);
    info.references = rpc::to<StorePathSet>(r.getReferences(), args...);
    info.registrationTime = r.getRegistrationTime();
    info.narSize = r.getNarSize();
    info.ultimate = r.getUltimate();
    info.sigs = rpc::to<StringSet>(r.getSigs());
    info.ca = rpc::from(r.getCa(), args...);
    return info;
}
}

template<>
struct Fill<daemon::LegacyProtocol::UnkeyedValidPathInfo, nix::UnkeyedValidPathInfo>
{
    static void fill(
        daemon::LegacyProtocol::UnkeyedValidPathInfo::Builder b,
        const nix::UnkeyedValidPathInfo & info,
        auto &&... args
    )
    {
        LIX_RPC_FILL_STRUCT(b, initDeriver, info.deriver, args...);
        LIX_RPC_FILL_STRUCT(b, initNarHash, info.narHash, args...);
        LIX_RPC_FILL_LIST(b, initReferences, info.references, args...);
        b.setRegistrationTime(info.registrationTime);
        b.setNarSize(info.narSize);
        b.setUltimate(info.ultimate);
        LIX_RPC_FILL_LIST(b, initSigs, info.sigs, args...);
        LIX_RPC_FILL_STRUCT(b, initCa, info.ca, args...);
    }
};

namespace daemon {
inline nix::ValidPathInfo from(const LegacyProtocol::ValidPathInfo::Reader & r, auto &&... args)
{
    nix::StorePath path = from(r.getPath(), args...);
    return nix::ValidPathInfo{std::move(path), from(r.getUnkeyedValidPathInfo(), args...)};
}
}

template<>
struct Fill<daemon::LegacyProtocol::ValidPathInfo, nix::ValidPathInfo>
{
    static void
    fill(daemon::LegacyProtocol::ValidPathInfo::Builder b, const nix::ValidPathInfo & info, auto &&... args)
    {
        LIX_RPC_FILL_STRUCT(
            b, initUnkeyedValidPathInfo, static_cast<const nix::UnkeyedValidPathInfo &>(info), args...
        );
        LIX_RPC_FILL_STRUCT(b, initPath, info.path, args...);
    }
};

namespace daemon {
inline GCOptions::GCAction from(LegacyProtocol::GCAction ht, auto &&...)
{
    switch (ht) {
    case LegacyProtocol::GCAction::RETURN_LIVE:
        return GCOptions::GCAction::gcReturnLive;
    case LegacyProtocol::GCAction::RETURN_DEAD:
        return GCOptions::GCAction::gcReturnDead;
    case LegacyProtocol::GCAction::DELETE_DEAD:
        return GCOptions::GCAction::gcDeleteDead;
    case LegacyProtocol::GCAction::DELETE_SPECIFIC:
        return GCOptions::GCAction::gcDeleteSpecific;
    case LegacyProtocol::GCAction::TRY_DELETE_SPECIFIC:
        return GCOptions::GCAction::gcTryDeleteSpecific;
    }
    throw nix::Error("invalid GCAction received over RPC: %d", uint16_t(ht));
}
}

inline daemon::LegacyProtocol::GCAction from(GCOptions::GCAction ht, auto &&...)
{
    switch (ht) {
    case GCOptions::GCAction::gcReturnLive:
        return daemon::LegacyProtocol::GCAction::RETURN_LIVE;
    case GCOptions::GCAction::gcReturnDead:
        return daemon::LegacyProtocol::GCAction::RETURN_DEAD;
    case GCOptions::GCAction::gcDeleteDead:
        return daemon::LegacyProtocol::GCAction::DELETE_DEAD;
    case GCOptions::GCAction::gcDeleteSpecific:
        return daemon::LegacyProtocol::GCAction::DELETE_SPECIFIC;
    case GCOptions::GCAction::gcTryDeleteSpecific:
        return daemon::LegacyProtocol::GCAction::TRY_DELETE_SPECIFIC;
    }
    abort(); // unreachable
}

namespace daemon {
inline nix::DerivedPathOpaque from(const LegacyProtocol::DerivedPathOpaque::Reader & r, auto &&... args)
{
    return nix::DerivedPathOpaque{from(r.getPath(), args...)};
}
}

template<>
struct Fill<daemon::LegacyProtocol::DerivedPathOpaque, nix::DerivedPathOpaque>
{
    static void fill(
        daemon::LegacyProtocol::DerivedPathOpaque::Builder b,
        const nix::DerivedPathOpaque & p,
        auto &&... args
    )
    {
        LIX_RPC_FILL_STRUCT(b, initPath, p.path, args...);
    }
};

namespace daemon {
inline nix::DerivedPathBuilt from(const LegacyProtocol::DerivedPathBuilt::Reader & r, auto &&... args)
{
    auto outputs = r.getOutputs();
    return nix::DerivedPathBuilt{
        from(r.getDrvPath(), args...),
        outputs.isAll() ? nix::OutputsSpec{nix::OutputsSpec::All{}}
                        : nix::OutputsSpec{
                              nix::OutputsSpec::Names{rpc::to<std::set<nix::OutputName>>(outputs.getNames())}
                          },
    };
}
}

template<>
struct Fill<daemon::LegacyProtocol::DerivedPathBuilt, nix::DerivedPathBuilt>
{
    static void fill(
        daemon::LegacyProtocol::DerivedPathBuilt::Builder b, const nix::DerivedPathBuilt & p, auto &&... args
    )
    {
        LIX_RPC_FILL_STRUCT(b, initDrvPath, p.drvPath, args...);
        auto outputs = b.getOutputs();
        std::visit(
            overloaded{
                [&](const nix::OutputsSpec::All &) { outputs.setAll(); },
                [&](const nix::OutputsSpec::Names & names) {
                    LIX_RPC_FILL_LIST(outputs, initNames, names, args...);
                },
            },
            p.outputs.raw
        );
    }
};

namespace daemon {
inline nix::DerivedPath from(const LegacyProtocol::DerivedPath::Reader & r, auto &&... args)
{
    auto raw = r.getRaw();
    if (raw.isOpaque()) {
        return nix::DerivedPath{from(raw.getOpaque(), args...)};
    } else {
        return nix::DerivedPath{from(raw.getBuilt(), args...)};
    }
}
}

template<>
struct Fill<daemon::LegacyProtocol::DerivedPath, nix::DerivedPath>
{
    static void
    fill(daemon::LegacyProtocol::DerivedPath::Builder b, const nix::DerivedPath & p, auto &&... args)
    {
        auto raw = b.getRaw();
        std::visit(
            overloaded{
                [&](const nix::DerivedPathOpaque & o) { LIX_RPC_FILL_STRUCT(raw, initOpaque, o, args...); },
                [&](const nix::DerivedPathBuilt & built) {
                    LIX_RPC_FILL_STRUCT(raw, initBuilt, built, args...);
                },
            },
            p.raw()
        );
    }
};

namespace daemon {
inline nix::BuildMode from(LegacyProtocol::BuildMode bm, auto &&...)
{
    switch (bm) {
    case LegacyProtocol::BuildMode::BM_NORMAL:
        return bmNormal;
    case LegacyProtocol::BuildMode::BM_REPAIR:
        return bmRepair;
    case LegacyProtocol::BuildMode::BM_CHECK:
        return bmCheck;
    }
    throw nix::Error("invalid BuildMode received over RPC: %d", uint16_t(bm));
}
}

inline daemon::LegacyProtocol::BuildMode from(nix::BuildMode bm, auto &&...)
{
    switch (bm) {
    case bmNormal:
        return daemon::LegacyProtocol::BuildMode::BM_NORMAL;
    case bmRepair:
        return daemon::LegacyProtocol::BuildMode::BM_REPAIR;
    case bmCheck:
        return daemon::LegacyProtocol::BuildMode::BM_CHECK;
    }
    abort(); // unreachable
}
}
