#include "lix/config.h"

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* Prevent a clash between some Perl and libstdc++ macros. */
#undef do_open
#undef do_close

#include "lix/libstore/derivations.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/crypto.hh"

#include <sodium.h>
#include <nlohmann/json.hpp>


using namespace nix;


static ref<Store> store()
{
    static std::shared_ptr<Store> _store;
    if (!_store) {
        try {
            initLibStore();
            _store = openStore();
        } catch (Error & e) {
            croak("%s", e.what());
        }
    }
    return ref<Store>(_store);
}


MODULE = Nix::Store PACKAGE = Nix::Store
PROTOTYPES: ENABLE


#undef dNOOP // Hack to work around "error: declaration of 'Perl___notused' has a different language linkage" error message on clang.
#define dNOOP


void init()
    CODE:
        store();


void setVerbosity(int level)
    CODE:
        verbosity = (Verbosity) level;


int isValidPath(char * path)
    CODE:
        try {
            RETVAL = store()->isValidPath(store()->parseStorePath(path));
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV * queryReferences(char * path)
    PPCODE:
        try {
            for (auto & i : store()->queryPathInfo(store()->parseStorePath(path))->references)
                XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryPathHash(char * path)
    PPCODE:
        try {
            auto s = store()->queryPathInfo(store()->parseStorePath(path))->narHash.to_string(Base::Base32, true);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryDeriver(char * path)
    PPCODE:
        try {
            auto info = store()->queryPathInfo(store()->parseStorePath(path));
            if (!info->deriver) XSRETURN_UNDEF;
            XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(*info->deriver).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryPathInfo(char * path, int base32)
    PPCODE:
        try {
            auto info = store()->queryPathInfo(store()->parseStorePath(path));
            if (!info->deriver)
                XPUSHs(&PL_sv_undef);
            else
                XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(*info->deriver).c_str(), 0)));
            auto s = info->narHash.to_string(base32 ? Base::Base32 : Base::Base16, true);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
            mXPUSHi(info->registrationTime);
            mXPUSHi(info->narSize);
            AV * refs = newAV();
            for (auto & i : info->references)
                av_push(refs, newSVpv(store()->printStorePath(i).c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) refs)));
            AV * sigs = newAV();
            for (auto & i : info->sigs)
                av_push(sigs, newSVpv(i.c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) sigs)));
        } catch (Error & e) {
            croak("%s", e.what());
        }

SV * queryRawRealisation(char * outputId)
    PPCODE:
      try {
        auto realisation = store()->queryRealisation(DrvOutput::parse(outputId));
        if (realisation)
            XPUSHs(sv_2mortal(newSVpv(realisation->toJSON().dump().c_str(), 0)));
        else
            XPUSHs(sv_2mortal(newSVpv("", 0)));
      } catch (Error & e) {
        croak("%s", e.what());
      }


SV * queryPathFromHashPart(char * hashPart)
    PPCODE:
        try {
            auto path = store()->queryPathFromHashPart(hashPart);
            XPUSHs(sv_2mortal(newSVpv(path ? store()->printStorePath(*path).c_str() : "", 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * computeFSClosure(int flipDirection, int includeOutputs, ...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 2; n < items; ++n)
                store()->computeFSClosure(store()->parseStorePath(SvPV_nolen(ST(n))), paths, flipDirection, includeOutputs);
            for (auto & i : paths)
                XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * topoSortPaths(...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 0; n < items; ++n) paths.insert(store()->parseStorePath(SvPV_nolen(ST(n))));
            auto sorted = store()->topoSortPaths(paths);
            for (auto & i : sorted)
                XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * followLinksToStorePath(char * path)
    CODE:
        try {
            RETVAL = newSVpv(store()->printStorePath(store()->followLinksToStorePath(path)).c_str(), 0);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void exportPaths(int fd, ...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 1; n < items; ++n) paths.insert(store()->parseStorePath(SvPV_nolen(ST(n))));
            FdSink sink(fd);
            store()->exportPaths(paths, sink);
        } catch (Error & e) {
            croak("%s", e.what());
        }


void importPaths(int fd, int dontCheckSigs)
    PPCODE:
        try {
            FdSource source(fd);
            store()->importPaths(source, dontCheckSigs ? NoCheckSigs : CheckSigs);
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashPath(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashPath(parseHashType(algo), path).first;
            auto s = h.to_string(base32 ? Base::Base32 : Base::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashFile(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashFile(parseHashType(algo), path);
            auto s = h.to_string(base32 ? Base::Base32 : Base::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashString(char * algo, int base32, char * s)
    PPCODE:
        try {
            Hash h = hashString(parseHashType(algo), s);
            auto s = h.to_string(base32 ? Base::Base32 : Base::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * convertHash(char * algo, char * s, int toBase32)
    PPCODE:
        try {
            auto h = Hash::parseAny(s, parseHashType(algo));
            auto s = h.to_string(toBase32 ? Base::Base32 : Base::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * signString(char * secretKey_, char * msg)
    PPCODE:
        try {
            auto sig = SecretKey(secretKey_).signDetached(msg);
            XPUSHs(sv_2mortal(newSVpv(sig.c_str(), sig.size())));
        } catch (Error & e) {
            croak("%s", e.what());
        }


int checkSignature(SV * publicKey_, SV * sig_, char * msg)
    CODE:
        try {
            STRLEN publicKeyLen;
            unsigned char * publicKey = (unsigned char *) SvPV(publicKey_, publicKeyLen);
            if (publicKeyLen != crypto_sign_PUBLICKEYBYTES)
                throw Error("public key is not valid");

            STRLEN sigLen;
            unsigned char * sig = (unsigned char *) SvPV(sig_, sigLen);
            if (sigLen != crypto_sign_BYTES)
                throw Error("signature is not valid");

            RETVAL = crypto_sign_verify_detached(sig, (unsigned char *) msg, strlen(msg), publicKey) == 0;
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV * addToStore(char * srcPath, int recursive, char * algo)
    PPCODE:
        try {
            auto method = recursive ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;
            auto path = store()->addToStore(std::string(baseNameOf(srcPath)), srcPath, method, parseHashType(algo));
            XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(path).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * makeFixedOutputPath(int recursive, char * algo, char * hash, char * name)
    PPCODE:
        try {
            auto h = Hash::parseAny(hash, parseHashType(algo));
            auto method = recursive ? FileIngestionMethod::Recursive : FileIngestionMethod::Flat;
            auto path = store()->makeFixedOutputPath(name, FixedOutputInfo {
                .method = method,
                .hash = h,
                .references = {},
            });
            XPUSHs(sv_2mortal(newSVpv(store()->printStorePath(path).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * derivationFromPath(char * drvPath)
    PREINIT:
        HV *hash;
    CODE:
        try {
            Derivation drv = store()->derivationFromPath(store()->parseStorePath(drvPath));
            hash = newHV();

            HV * outputs = newHV();
            for (auto & i : drv.outputsAndOptPaths(*store())) {
                hv_store(
                    outputs, i.first.c_str(), i.first.size(),
                    !i.second.second
                        ? newSV(0) /* null value */
                        : newSVpv(store()->printStorePath(*i.second.second).c_str(), 0),
                    0);
            }
            hv_stores(hash, "outputs", newRV((SV *) outputs));

            AV * inputDrvs = newAV();
            for (auto & i : drv.inputDrvs.map)
                av_push(inputDrvs, newSVpv(store()->printStorePath(i.first).c_str(), 0)); // !!! ignores i->second
            hv_stores(hash, "inputDrvs", newRV((SV *) inputDrvs));

            AV * inputSrcs = newAV();
            for (auto & i : drv.inputSrcs)
                av_push(inputSrcs, newSVpv(store()->printStorePath(i).c_str(), 0));
            hv_stores(hash, "inputSrcs", newRV((SV *) inputSrcs));

            hv_stores(hash, "platform", newSVpv(drv.platform.c_str(), 0));
            hv_stores(hash, "builder", newSVpv(drv.builder.c_str(), 0));

            AV * args = newAV();
            for (auto & i : drv.args)
                av_push(args, newSVpv(i.c_str(), 0));
            hv_stores(hash, "args", newRV((SV *) args));

            HV * env = newHV();
            for (auto & i : drv.env)
                hv_store(env, i.first.c_str(), i.first.size(), newSVpv(i.second.c_str(), 0), 0);
            hv_stores(hash, "env", newRV((SV *) env));

            RETVAL = newRV_noinc((SV *)hash);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void addTempRoot(char * storePath)
    PPCODE:
        try {
            store()->addTempRoot(store()->parseStorePath(storePath));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * getBinDir()
    PPCODE:
        XPUSHs(sv_2mortal(newSVpv(settings.nixBinDir.c_str(), 0)));


SV * getStoreDir()
    PPCODE:
        XPUSHs(sv_2mortal(newSVpv(settings.nixStore.c_str(), 0)));
