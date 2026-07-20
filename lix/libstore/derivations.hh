#pragma once
///@file

#include "lix/libstore/path.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/hash.hh"
#include "lix/libstore/content-address.hh"
#include "lix/libutil/repair-flag.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/comparator.hh"
#include "lix/libutil/variant-wrapper.hh"
#include "outputs-spec.hh"

#include <kj/async.h>
#include <map>
#include <variant>


namespace nix {

class Store;

/* Abstract syntax of derivations. */

/**
 * A single output of a BasicDerivation (and Derivation).
 */
struct DerivationOutput
{
    /**
     * The traditional non-fixed-output derivation type.
     */
    struct InputAddressed
    {
        StorePath path;

        GENERATE_CMP(InputAddressed, me->path);
    };

    /**
     * Fixed-output derivations, whose output paths are content
     * addressed according to that fixed output.
     */
    struct CAFixed
    {
        /**
         * Method and hash used for expected hash computation.
         *
         * References are not allowed by fiat.
         */
        ContentAddress ca;

        /**
         * Return the \ref StorePath "store path" corresponding to this output
         *
         * @param drvName The name of the derivation this is an output of, without the `.drv`.
         * @param outputName The name of this output.
         */
        StorePath path(const Store & store, std::string_view drvName, OutputNameView outputName) const;

        GENERATE_CMP(CAFixed, me->ca);
    };

    typedef std::variant<
        InputAddressed,
        CAFixed
    > Raw;

    Raw raw;

    GENERATE_CMP(DerivationOutput, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(DerivationOutput);

    /**
     * Force choosing a variant
     */
    DerivationOutput() = delete;

    /**
     * \note when you use this function you should make sure that you're
     * passing the right derivation name. When in doubt, you should use
     * the safer interface provided by
     * BasicDerivation::outputsAndPaths
     */
    StorePath path(const Store & store, std::string_view drvName, OutputNameView outputName) const;

    JSON toJSON(
        const Store & store,
        std::string_view drvName,
        OutputNameView outputName) const;
    /**
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DerivationOutput fromJSON(
        const Store & store,
        std::string_view drvName,
        OutputNameView outputName,
        const JSON & json,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
};

typedef std::map<std::string, DerivationOutput> DerivationOutputs;

/**
 * These are analogues to the previous DerivationOutputs data type,
 * but they also contains, for each output, the store
 * path in which it would be written. To calculate values of these
 * types, see the corresponding functions in BasicDerivation.
 */
typedef std::map<std::string, std::pair<DerivationOutput, StorePath>>
  DerivationOutputsAndPaths;

/**
 * For inputs that are sub-derivations, we specify exactly which
 * output IDs we are interested in.
 */
typedef std::map<StorePath, StringSet> DerivationInputs;

struct DerivationType {
    /**
     * Input-addressed derivation types
     */
    struct InputAddressed {
        GENERATE_CMP(InputAddressed);
    };

    /**
     * Content-addressed derivation types
     */
    struct ContentAddressed {
        GENERATE_CMP(ContentAddressed);
    };

    typedef std::variant<
        InputAddressed,
        ContentAddressed
    > Raw;

    Raw raw;

    GENERATE_CMP(DerivationType, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(DerivationType);

    /**
     * Force choosing a variant
     */
    DerivationType() = delete;

    /**
     * Do the outputs of the derivation have paths calculated from their
     * content, or from the derivation itself?
     */
    bool isCA() const;

    /**
     * Is the content of the outputs fixed <em>a priori</em> via a hash?
     * Never true for non-CA derivations.
     */
    bool isFixed() const;

    /**
     * Whether the derivation is fully sandboxed. If false, the sandbox
     * is opened up, e.g. the derivation has access to the network. Note
     * that whether or not we actually sandbox the derivation is
     * controlled separately. Always true for non-CA derivations.
     */
    bool isSandboxed() const;
};

struct BasicDerivation
{
    /**
     * keyed on symbolic IDs
     */
    DerivationOutputs outputs;
    /**
     * inputs that are sources
     */
    StorePathSet inputSrcs;
    std::string platform;
    Path builder;
    Strings args;
    StringPairs env;
    std::string name;

    BasicDerivation() = default;
    virtual ~BasicDerivation() { };

    bool isBuiltin() const;

    /**
     * Return true iff this is a fixed-output derivation.
     */
    DerivationType type() const;

    /**
     * Return the output names of a derivation.
     */
    StringSet outputNames() const;

    /**
     * Calculates the maps that contains all the DerivationOutputs, but
     * augmented with knowledge of the Store paths they would be written
     * into.
     */
    DerivationOutputsAndPaths outputsAndPaths(const Store & store) const;

    static std::string_view nameFromPath(const StorePath & storePath);

    GENERATE_CMP(BasicDerivation,
        me->outputs,
        me->inputSrcs,
        me->platform,
        me->builder,
        me->args,
        me->env,
        me->name);
};

struct Derivation : BasicDerivation
{
    /**
     * inputs that are sub-derivations
     */
    std::map<StorePath, std::set<OutputName>> inputDrvs;

    /**
     * Print a derivation.
     */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet> * actualInputs = nullptr) const;

    /**
     * Check that the derivation is valid and does not present any
     * illegal states.
     *
     * This is mainly a matter of checking the outputs, where our C++
     * representation supports all sorts of combinations we do not yet
     * allow.
     */
    kj::Promise<Result<void>> checkInvariants(Store & store, const StorePath & drvPath) const;

    Derivation() = default;
    Derivation(const BasicDerivation & bd) : BasicDerivation(bd) { }
    Derivation(BasicDerivation && bd) : BasicDerivation(std::move(bd)) { }

    JSON toJSON(const Store & store) const;
    static Derivation fromJSON(
        const Store & store,
        const JSON & json,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    GENERATE_CMP(Derivation,
        static_cast<const BasicDerivation &>(*me),
        me->inputDrvs);
};


class Store;

/**
 * Write a derivation to the Nix store, and return its path.
 */
kj::Promise<Result<StorePath>> writeDerivation(Store & store,
    const Derivation & drv,
    RepairFlag repair = NoRepair,
    bool readOnly = false);

/**
 * Read a derivation from a file.
 */
Derivation parseDerivation(
    const Store & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * \todo Remove.
 *
 * Use Path::isDerivation instead.
 */
bool isDerivation(std::string_view fileName);

/**
 * Calculate the name that will be used for the store path for this
 * output.
 *
 * This is usually <drv-name>-<output-name>, but is just <drv-name> when
 * the output name is "out".
 */
std::string outputPathName(std::string_view drvName, OutputNameView outputName);


/**
 * The hashes modulo of a derivation.
 *
 * Each output is given a hash, although in practice only the content-addressed
 * derivations (i.e. fixed-output) will have a different hash for each
 * output.
 */
struct DrvHash {
    /**
     * Map from output names to hashes
     */
    std::map<std::string, Hash> hashes;
};

/**
 * Returns hashes with the details of fixed-output subderivations
 * expunged.
 *
 * A fixed-output derivation is a derivation whose outputs have a
 * specified content hash and hash algorithm. (Currently they must have
 * exactly one output (`out`), which is specified using the `outputHash`
 * and `outputHashAlgo` attributes, but the algorithm doesn't assume
 * this.) We don't want changes to such derivations to propagate upwards
 * through the dependency graph, changing output paths everywhere.
 *
 * For instance, if we change the url in a call to the `fetchurl`
 * function, we do not want to rebuild everything depending on it---after
 * all, (the hash of) the file being downloaded is unchanged.  So the
 * *output paths* should not change. On the other hand, the *derivation
 * paths* should change to reflect the new dependency graph.
 *
 * For fixed-output derivations, this returns a map from the name of
 * each output to its hash, unique up to the output's contents.
 *
 * For regular derivations, it returns a single hash of the derivation
 * ATerm, after subderivations have been likewise expunged from that
 * derivation.
 */
kj::Promise<Result<DrvHash>>
hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/**
 * Return a map associating each output to a hash that uniquely identifies its
 * derivation (modulo the self-references).
 *
 * \todo What is the Hash in this map?
 */
kj::Promise<Result<std::map<std::string, Hash>>>
staticOutputHashes(Store & store, const Derivation & drv);

/**
 * Memoisation of hashDerivationModulo().
 */
typedef std::map<StorePath, DrvHash> DrvHashes;

// FIXME: global, though at least thread-safe.
extern Sync<DrvHashes> drvHashes;

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv, std::string_view name);
WireFormatGenerator serializeDerivation(const Store & store, const BasicDerivation & drv);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

/**
 * This creates an opaque and almost certainly unique string
 * deterministically from the output name.
 *
 * It is used as a placeholder to allow derivations to refer to their
 * own outputs without needing to use the hash of a derivation in
 * itself, making the hash near-impossible to calculate.
 */
std::string hashPlaceholder(const OutputNameView outputName);

}
