#pragma once
///@file

#include "lix/libutil/hash.hh"
#include "lix/libexpr/eval.hh"

#include <functional>
#include <variant>

namespace nix::eval_cache {

struct AttrDb;
class AttrCursor;

typedef std::function<Value(EvalState &)> RootLoader;

/**
 * EvalState with caching support. Historically this was part of EvalState,
 * but it was split out to make maintenance easier. This could've been just
 * a pair of EvalState and the cache map, but doing so would currently hide
 * the rather strong connection between EvalState and these caches. At some
 * future time the cache interface should be changed to hide its EvalState.
 */
class CachingEvaluator : public Evaluator
{
    /**
     * A cache for evaluation caches, so as to reuse the same root value if possible
     */
    std::map<Hash, ref<EvalCache>> caches;

public:
    using Evaluator::Evaluator;

    ref<EvalCache> getCacheFor(Hash hash, RootLoader rootLoader);
};

class EvalCache : public std::enable_shared_from_this<EvalCache>
{
    friend class AttrCursor;

    std::shared_ptr<AttrDb> db;
    RootLoader rootLoader;
    RootValue value;

    Value & getRootValue(EvalState & state);

public:

    EvalCache(
        std::optional<std::reference_wrapper<const Hash>> useCache,
        RootLoader rootLoader);

    ref<AttrCursor> getRoot();
};

enum AttrType {
    Placeholder = 0,
    FullAttrs = 1,
    String = 2,
    Missing = 3,
    Misc = 4,
    Failed = 5,
    Bool = 6,
    ListOfStrings = 7,
    Int = 8,
};

struct placeholder_t {};
struct fullattr_t { std::vector<std::string> p; };
struct missing_t {};
struct misc_t {};
struct failed_t {};
struct int_t { NixInt x; };
typedef uint64_t AttrId;
typedef std::pair<AttrId, std::string> AttrKey;
typedef std::pair<std::string, NixStringContext> string_t;

typedef std::variant<
    fullattr_t,
    string_t,
    placeholder_t,
    missing_t,
    misc_t,
    failed_t,
    bool,
    int_t,
    std::vector<std::string>
    > AttrValue;

class AttrCursor : public std::enable_shared_from_this<AttrCursor>
{
    friend class EvalCache;

    ref<EvalCache> root;
    typedef std::optional<std::pair<std::shared_ptr<AttrCursor>, std::string>> Parent;
    Parent parent;
    RootValue _value;
    std::optional<std::pair<AttrId, AttrValue>> cachedValue;

    AttrKey getKey();

    Value & getValue(EvalState & state);

public:

    AttrCursor(
        ref<EvalCache> root,
        Parent parent,
        Value * value = nullptr,
        std::optional<std::pair<AttrId, AttrValue>> && cachedValue = {});

    std::vector<std::string> getAttrPath(EvalState & state) const;

    std::vector<std::string> getAttrPath(EvalState & state, std::string_view name) const;

    std::string getAttrPathStr(EvalState & state) const;

    std::string getAttrPathStr(EvalState & state, std::string_view name) const;

    Suggestions getSuggestionsForAttr(EvalState & state, const std::string & name);

    std::shared_ptr<AttrCursor> maybeGetAttr(EvalState & state, const std::string & name);

    ref<AttrCursor> getAttr(EvalState & state, const std::string & name);

    /**
     * Get an attribute along a chain of attrsets. Note that this does
     * not auto-call functors or functions.
     */
    OrSuggestions<ref<AttrCursor>> findAlongAttrPath(EvalState & state, const std::vector<std::string> & attrPath);

    std::string getString(EvalState & state);

    string_t getStringWithContext(EvalState & state);

    bool getBool(EvalState & state);

    NixInt getInt(EvalState & state);

    std::vector<std::string> getListOfStrings(EvalState & state);

    std::vector<std::string> getAttrs(EvalState & state);

    bool isDerivation(EvalState & state);

    Value & forceValue(EvalState & state);

    /**
     * Force creation of the .drv file in the Nix store.
     */
    StorePath forceDerivation(EvalState & state);
};

}
