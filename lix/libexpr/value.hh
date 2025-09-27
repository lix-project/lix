#pragma once
///@file

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>

#include "lix/libexpr/gc-alloc.hh"
#include "lix/libexpr/value/context.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/source-path.hh"
#include "lix/libexpr/print-options.hh"
#include "lix/libutil/checked-arithmetic.hh"
#include "lix/libutil/concepts.hh"
#include "lix/libutil/json-fwd.hh"

namespace nix {

class BindingsBuilder;
class EvalMemory;
class EvalState;
struct Value;

/**
 * Function that implements a primop.
 */
using PrimOpImpl = void(EvalState & state, Value ** args, Value & v);

/**
 * Info about a primitive operation, and its implementation
 */
struct PrimOpDetails
{
    /**
     * Name of the primop. `__` prefix is treated specially.
     */
    std::string name;

    /**
     * Names of the parameters of a primop, for primops that take a
     * fixed number of arguments to be substituted for these parameters.
     */
    std::vector<std::string> args;

    /**
     * Aritiy of the primop.
     *
     * If `args` is not empty, this field will be computed from that
     * field instead, so it doesn't need to be manually set.
     */
    size_t arity = 0;

    /**
     * Optional free-form documentation about the primop.
     */
    const char * doc = nullptr;

    /**
     * Implementation of the primop.
     */
    std::function<PrimOpImpl> fun;

    /**
     * Optional experimental for this to be gated on.
     */
    std::optional<ExperimentalFeature> experimentalFeature;
};

// NOTE value.cc contains alignment assertions for pointers tagged thusly.
// *always* ensure that these assertions match the tag types declared here
typedef enum {
    // NOTE: tThunk *must* be 0, otherwise invalid value detection breaks
    // since invalid values are encoded as thunks with a null thunk state
    tThunk = 0,
    tApp,
    tInt,
    tBool,
    tString,
    tAttrs,
    tList,
    tAuxiliary,
} InternalType;

/**
 * This type abstracts over all actual value types in the language,
 * grouping together implementation details like tList*, different function
 * types, and types in non-normal form (so thunks and co.)
 */
typedef enum {
    nThunk,
    nInt,
    nFloat,
    nBool,
    nString,
    nPath,
    nNull,
    nAttrs,
    nList,
    nFunction,
    nExternal
} ValueType;


/**
 * Modes of string coercion.
 *
 * Determines how permissive the coercion functions are when converting
 * values to strings.
 *
 * - Strict: Only allow coercion of values that are already strings,
 *   paths, or derivations.
 * - Interpolation: Additionally allow coercion of unambiguously printable values in a string, for
 *   now: integers. This mode is meant as a stopgap measure until we get better formatting tools.
 * - ToString: Additionally allow coercion of integers, booleans, null,
 *   and lists to strings.
 */
enum class StringCoercionMode {
    Strict,
    Interpolation,
    ToString,
};

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct ExprBlackHole;
class PosIdx;
struct Pos;
class StorePath;
class Store;
class EvalState;
class XMLWriter;
class Printer;

using NixInt = checked::Checked<int64_t>;
using NixFloat = double;

/**
 * External values must descend from ExternalValueBase, so that
 * type-agnostic nix functions (e.g. showType) can be implemented
 */
class ExternalValueBase
{
    friend std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);
    friend class Printer;
    protected:
    /**
     * Print out the value
     */
    virtual std::ostream & print(std::ostream & str) const = 0;

    public:
    /**
     * Return a simple string describing the type
     */
    virtual std::string showType() const = 0;

    /**
     * Return a string to be used in builtins.typeOf
     */
    virtual std::string typeOf() const = 0;

    /**
     * Coerce the value to a string. Defaults to uncoercable, i.e. throws an
     * error.
     */
    virtual std::string coerceToString(EvalState & state, const PosIdx & pos, NixStringContext & context, StringCoercionMode mode, bool copyToStore) const;

    /**
     * Compare to another value of the same type. Defaults to uncomparable,
     * i.e. always false.
     */
    virtual bool operator ==(const ExternalValueBase & b) const;

    /**
     * Print the value as JSON. Defaults to unconvertable, i.e. throws an error
     */
    virtual JSON printValueAsJSON(EvalState & state, bool strict,
        NixStringContext & context, bool copyToStore = true) const;

    /**
     * Print the value as XML. Defaults to unevaluated
     */
    virtual void printValueAsXML(EvalState & state, bool strict, bool location,
        XMLWriter & doc, NixStringContext & context, PathSet & drvsSeen,
        const PosIdx pos) const;

    virtual ~ExternalValueBase()
    {
    };
};

std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);

struct NewValueAs
{
    struct integer_t { };
    constexpr static integer_t integer{};

    struct floating_t { };
    constexpr static floating_t floating{};

    struct boolean_t { };
    constexpr static boolean_t boolean{};

    struct string_t { };
    constexpr static string_t string{};

    struct path_t { };
    constexpr static path_t path{};

    struct list_t { };
    constexpr static list_t list{};

    struct attrs_t { };
    constexpr static attrs_t attrs{};

    struct thunk_t { };
    constexpr static thunk_t thunk{};

    struct null_t { };
    constexpr static null_t null{};

    struct app_t { };
    constexpr static app_t app{};

    struct primop_t { };
    constexpr static primop_t primop{};

    struct lambda_t { };
    constexpr static lambda_t lambda{};

    struct external_t { };
    constexpr static external_t external{};

    struct blackhole_t { };
    constexpr static blackhole_t blackhole{};
};

struct Value
{
private:
    mutable uintptr_t raw;

public:
    static constexpr size_t TAG_BITS = 3;
    static constexpr size_t TAG_ALIGN = 1 << TAG_BITS;
    static constexpr uintptr_t TAG_MASK = (1 << TAG_BITS) - 1;

private:
    // boehmgc always allocate in two-word chunks, which means 8 bytes on 32 bit architectures.
    // ensure that malloc must always use at least 8 byte chunks as well so our tags always fit
    static_assert(alignof(std::max_align_t) >= Value::TAG_ALIGN);

    static uintptr_t tag(InternalType t, auto v)
    {
        if constexpr (std::is_null_pointer_v<decltype(v)>) {
            return t;
        } else if constexpr (std::is_pointer_v<decltype(v)>) {
            return (reinterpret_cast<uintptr_t>(v)) | t;
        } else {
            return (static_cast<uintptr_t>(v) << TAG_BITS) | t;
        }
    }

    template<typename T>
    T untag() const
    {
        if constexpr (std::is_pointer_v<T>) {
            return reinterpret_cast<T>(raw & ~TAG_MASK);
        } else {
            return static_cast<T>((raw & ~TAG_MASK) >> TAG_BITS);
        }
    }

    InternalType internalType() const
    {
        return InternalType(raw & TAG_MASK);
    }

    friend std::string showType(const Value & v);

public:

    /**
     * Empty list constant.
     */
    static Value EMPTY_LIST;

    struct String;
    struct Acb;
    struct Null;
    struct Lambda;
    struct Thunk;
    struct Int;

    static const Null NULL_ACB;

    /** Single, unforceable black hole thunk control block. */
    static Thunk blackHole;

    // Discount `using NewValueAs::*;`
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define USING_VALUETYPE(name) using name = NewValueAs::name
    USING_VALUETYPE(integer_t);
    USING_VALUETYPE(floating_t);
    USING_VALUETYPE(boolean_t);
    USING_VALUETYPE(string_t);
    USING_VALUETYPE(path_t);
    USING_VALUETYPE(list_t);
    USING_VALUETYPE(attrs_t);
    USING_VALUETYPE(thunk_t);
    USING_VALUETYPE(primop_t);
    USING_VALUETYPE(app_t);
    USING_VALUETYPE(null_t);
    USING_VALUETYPE(lambda_t);
    USING_VALUETYPE(external_t);
    USING_VALUETYPE(blackhole_t);
#undef USING_VALUETYPE

    struct List;
    struct PrimOp;

    static bool isTaggableInteger(NixInt i)
    {
        return i.value <= (std::numeric_limits<intptr_t>::max() >> 3)
            && i.value >= (std::numeric_limits<intptr_t>::min() >> 3);
    }

    /// Default constructor which is still used in the codebase but should not
    /// be used in new code. Zero initializes its members.
    [[deprecated]]
    Value()
        : raw{0}
    {
    }

    /// Constructs a nix language value of type "int", with the integral value
    /// of @ref i.
    Value(integer_t, NixInt i)
    {
        if (isTaggableInteger(i)) {
            raw = tInt | (uintptr_t(i.value) << TAG_BITS);
        } else {
            auto ip = gcAllocType<Int>();
            ip->raw = Acb::tInt;
            ip->value = i;
            raw = tag(tAuxiliary, ip);
        }
    }

    /// Constructs a nix language value of type "float", with the floating
    /// point value of @ref f.
    Value(floating_t, NixFloat f)
    {
        auto fp = gcAllocType<Float>();
        fp->raw = Acb::tFloat;
        fp->value = f;
        raw = tag(tAuxiliary, fp);
    }

    /// Constructs a nix language value of type "bool", with the boolean
    /// value of @ref b.
    Value(boolean_t, bool b) : raw(tag(tBool, b)) {}

    /// Constructs a nix language value of type "string", with the value of the
    /// C-string pointed to by @ref strPtr, and optionally with an array of
    /// string context pointed to by @ref contextPtr.
    ///
    /// Neither the C-string nor the context array are copied; this constructor
    /// assumes suitable memory has already been allocated (with the GC if
    /// enabled), and string and context data copied into that memory.
    Value(string_t, char const * strPtr, char const ** contextPtr = nullptr)
    {
        auto block = gcAllocType<String>();
        *block = {.content = strPtr, .context = contextPtr};
        raw = tag(tString, block);
    }

    Value(string_t, const String * str) : raw(tag(tString, str)) {}

    /// Constructx a nix language value of type "string", with a copy of the
    /// string data viewed by @ref copyFrom.
    ///
    /// The string data *is* copied from @ref copyFrom, and this constructor
    /// performs a dynamic (GC) allocation to do so.
    Value(string_t, std::string_view copyFrom, NixStringContext const & context = {})
    {
        auto block = gcAllocType<String>();
        *block = {.content = gcCopyStringIfNeeded(copyFrom), .context = nullptr};
        raw = tag(tString, block);

        if (context.empty()) {
            // It stays nullptr.
            return;
        }

        // Copy the context.
        block->context = gcAllocType<char const *>(context.size() + 1);

        size_t n = 0;
        for (NixStringContextElem const & contextElem : context) {
            block->context[n] = gcCopyStringIfNeeded(contextElem.to_string());
            n += 1;
        }

        // Terminator sentinel.
        block->context[n] = nullptr;
    }

    /// Constructx a nix language value of type "string", with the value of the
    /// C-string pointed to by @ref strPtr, and optionally with a set of string
    /// context @ref context.
    ///
    /// The C-string is not copied; this constructor assumes suitable memory
    /// has already been allocated (with the GC if enabled), and string data
    /// has been copied into that memory. The context data *is* copied from
    /// @ref context, and this constructor performs a dynamic (GC) allocation
    /// to do so.
    Value(string_t, char const * strPtr, NixStringContext const & context)
    {
        auto block = gcAllocType<String>();
        *block = {.content = strPtr, .context = nullptr};
        raw = tag(tString, block);

        if (context.empty()) {
            // It stays nullptr
            return;
        }

        // Copy the context.
        block->context = gcAllocType<char const *>(context.size() + 1);

        size_t n = 0;
        for (NixStringContextElem const & contextElem : context) {
            block->context[n] = gcCopyStringIfNeeded(contextElem.to_string());
            n += 1;
        }

        // Terminator sentinel.
        block->context[n] = nullptr;
    }

    /// Constructs a nix language value of type "path", with the value of the
    /// C-string pointed to by @ref strPtr.
    ///
    /// The C-string is not copied; this constructor assumes suitable memory
    /// has already been allocated (with the GC if enabled), and string data
    /// has been copied into that memory.
    Value(path_t, const String * str) : raw(tag(tString, str))
    {
        assert(str->isPath());
    }

    /// Constructs a nix language value of type "path", with the path
    /// @ref path.
    ///
    /// The data from @ref path *is* copied, and this constructor performs a
    /// dynamic (GC) allocation to do so.
    Value(path_t, SourcePath const & path)
    {
        auto block = gcAllocType<String>();
        *block = {.content = gcCopyStringIfNeeded(path.canonical().abs()), .context = String::path};
        raw = tag(tString, block);
    }

    /// Constructs a nix language value of type "list", with element array
    /// @ref items.
    ///
    /// Generally, the data in @ref items is neither deep copied nor shallow
    /// copied. This construct assumes the std::span @ref items is a region of
    /// memory that has already been allocated (with the GC if enabled), and
    /// an array of valid Value pointers has been copied into that memory.
    ///
    /// Howver, as an implementation detail, if @ref items is only 2 items or
    /// smaller, the list is stored inline, and the Value pointers in
    /// @ref items are shallow copied into this structure, without dynamically
    /// allocating memory.
    Value(list_t, const List * items) : raw(tag(tList, items)) {}

    /// Constructs a nix language value of the singleton type "null".
    Value(null_t) : raw(tag(tAuxiliary, &NULL_ACB)) {}

    /// Constructs a nix language value of type "set", with the attribute
    /// bindings pointed to by @ref bindings.
    ///
    /// The bindings are not not copied; this constructor assumes @ref bindings
    /// has already been suitably allocated by something like nix::buildBindings.
    Value(attrs_t, Bindings * bindings) : raw(tag(tAttrs, bindings)) {}

    /// Constructs a nix language lazy delayed computation, or "thunk".
    ///
    /// The thunk stores the environment it will be computed in @ref env, and
    /// the expression that will need to be evaluated @ref expr.
    Value(thunk_t, EvalMemory & mem, Env & env, Expr & expr);

    /// Constructs a nix language value of type "lambda", which represents
    /// a builtin, primitive operation ("primop"), from the primop
    /// implemented by @ref primop.
    Value(primop_t, PrimOp & primop);

    /// Constructs a nix language value of type "lambda", which represents a
    /// lazy and/or partial application of a function.
    Value(app_t, EvalMemory & mem, Value & lhs, Value & rhs);

    /// Constructs a nix language value of type "lambda", which represents a
    /// lazy and/or partial application of a function.
    Value(app_t, EvalMemory & mem, Value & lhs, std::span<Value> args);

    /// Constructs a nix language value of type "external", which is only used
    /// by plugins. Do any existing plugins even use this mechanism?
    Value(external_t, ExternalValueBase & external)
    {
        auto ext = gcAllocType<External>();
        ext->raw = Acb::tExternal;
        ext->external = &external;
        raw = tag(tAuxiliary, ext);
    }

    /// Constructs a nix language value of type "lambda", which represents a
    /// run of the mill lambda defined in nix code.
    ///
    /// This takes the environment the lambda is closed over @ref env, and
    /// the lambda expression itself @ref lambda, which will not be evaluated
    /// until it is applied.
    Value(lambda_t, EvalMemory & mem, Env & env, ExprLambda & lambda);

    /// Constructs an evil thunk, whose evaluation represents infinite recursion.
    explicit Value(blackhole_t) : raw(tag(tThunk, &blackHole)) {}

    explicit Value(const Acb & backing) : raw(tag(tAuxiliary, &backing)) {}

    Value(Value const & rhs) = default;

    /// Move constructor. Does the same thing as the copy constructor, but
    /// also zeroes out the other Value.
    Value(Value && rhs) : raw(0)
    {
        *this = std::move(rhs);
    }

    Value & operator=(Value const & rhs) = default;

    /// Move assignment operator.
    /// Does the same thing as the copy assignment operator, but also zeroes out
    /// the rhs.
    inline Value & operator=(Value && rhs)
    {
        *this = static_cast<const Value &>(rhs);
        if (this != &rhs) {
            // Kill `rhs`, because non-destructive move lol.
            rhs.raw = 0;
        }
        return *this;
    }

    void print(EvalState &state, std::ostream &str, PrintOptions options = PrintOptions {});

    // Functions needed to distinguish the type
    // These should be removed eventually, by putting the functionality that's
    // needed by callers into methods of this type

    // type() == nThunk
    inline bool isThunk() const
    {
        return internalType() == tThunk;
    };
    inline bool isApp() const
    {
        return internalType() == tApp;
    }
    inline bool isBlackhole() const;
    inline bool isInvalid() const
    {
        return raw == 0;
    }

    // type() == nFunction
    inline bool isLambda() const
    {
        return internalType() == tAuxiliary && auxiliary()->type() == Acb::tLambda;
    };
    inline bool isPrimOp() const
    {
        return internalType() == tAuxiliary && auxiliary()->type() == Acb::tPrimOp;
    }
    inline bool isPrimOpApp() const;

    /**
     * Strings in the evaluator carry a so-called `context` which
     * is a list of strings representing store paths.  This is to
     * allow users to write things like

     *   "--with-freetype2-library=" + freetype + "/lib"

     * where `freetype` is a derivation (or a source to be copied
     * to the store).  If we just concatenated the strings without
     * keeping track of the referenced store paths, then if the
     * string is used as a derivation attribute, the derivation
     * will not have the correct dependencies in its inputDrvs and
     * inputSrcs.

     * The semantics of the context is as follows: when a string
     * with context C is used as a derivation attribute, then the
     * derivations in C will be added to the inputDrvs of the
     * derivation, and the other store paths in C will be added to
     * the inputSrcs of the derivations.

     * For canonicity, the store paths should be in sorted order.
     */
    struct alignas(TAG_ALIGN) String
    {
        /// marker location for paths, to be used as path context.
        static inline const char * path[] = {"\1<path>", nullptr};

        const char * content;
        const char ** context; // must be in sorted order

        bool isPath() const
        {
            return context == path;
        }
    };

    struct App;

    /// auxiliary control block for values that require more space.
    /// these blocks are usually heap-allocated in GC memory space.
    struct alignas(TAG_ALIGN) Acb
    {
        // NOTE value.cc contains alignment assertions for pointers tagged thusly.
        // *always* ensure that these assertions match the tag types declared here
        enum Type {
            tExternal,
            tFloat,
            tNull,
            tPrimOp,
            tLambda,
            tInt,
        };

        uintptr_t raw;

        static constexpr size_t TAG_BITS = 3;
        static constexpr size_t TAG_ALIGN = 1 << TAG_BITS;
        static constexpr uintptr_t TAG_MASK = (1 << TAG_BITS) - 1;

        static uintptr_t tag(Type t, auto v)
        {
            if constexpr (std::is_null_pointer_v<decltype(v)>) {
                return t;
            } else if constexpr (std::is_pointer_v<decltype(v)>) {
                return (reinterpret_cast<uintptr_t>(v)) | t;
            } else {
                return (static_cast<uintptr_t>(v) << TAG_BITS) | t;
            }
        }

        template<typename T>
        T untag() const
        {
            if constexpr (std::is_pointer_v<T>) {
                return reinterpret_cast<T>(raw & ~TAG_MASK);
            } else {
                return static_cast<T>((raw & ~TAG_MASK) >> TAG_BITS);
            }
        }

        Type type() const
        {
            return Type(raw & TAG_MASK);
        }
    };
    struct External : Acb
    {
        ExternalValueBase * external;
    };
    struct Float : Acb
    {
        NixFloat value;
    };
    struct Null : Acb
    {};
    struct PrimOp : Acb, PrimOpDetails
    {
        explicit PrimOp(PrimOpDetails p) : Acb{tPrimOp}, PrimOpDetails(std::move(p)) {}
    };
    struct Int : Acb
    {
        NixInt value;
    };

    struct Lambda : Acb
    {
        ExprLambda * fun;

        Lambda(Env & env, ExprLambda & fun) : Acb{tag(tLambda, &env)}, fun(&fun) {}

        Env * env() const
        {
            return untag<Env *>();
        }
    };

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     *
     * @param invalidIsThunk Instead of aborting an an invalid (probably
     * 0, so uninitialized) internal type, return `nThunk`.
     */
    inline ValueType type(bool invalidIsThunk = false) const;

    inline void mkInt(NixInt::Inner n)
    {
        mkInt(NixInt{n});
    }

    inline void mkInt(NixInt n)
    {
        *this = {NewValueAs::integer, n};
    }

    inline void mkBool(bool b)
    {
        raw = tag(tBool, b);
    }

    inline void mkString(const char * s, const char * * context = 0)
    {
        auto block = gcAllocType<String>();
        *block = {.content = s, .context = context};
        raw = tag(tString, block);
    }

    void mkString(std::string_view s);

    void mkString(std::string_view s, const NixStringContext & context);

    void mkStringMove(const char * s, const NixStringContext & context);

    void mkPath(const SourcePath & path);

    inline void mkPath(const char * path)
    {
        auto block = gcAllocType<String>();
        *block = {.content = path, .context = String::path};
        raw = tag(tString, block);
    }

    inline void mkNull()
    {
        *this = {NewValueAs::null};
    }

    inline void mkAttrs(Bindings * a)
    {
        raw = tag(tAttrs, a);
    }

    Value & mkAttrs(BindingsBuilder & bindings);

    void mkPrimOp(PrimOp * p);

    inline void mkExternal(ExternalValueBase * e)
    {
        *this = {NewValueAs::external, *e};
    }

    inline void mkFloat(NixFloat n)
    {
        *this = {NewValueAs::floating, n};
    }

    bool isList() const
    {
        return internalType() == tList;
    }

    Value * listElems() const;

    size_t listSize() const;

    /**
     * Check whether forcing this value requires a trivial amount of
     * computation. In particular, function applications are
     * non-trivial.
     */
    bool isTrivial() const;

    auto listItems() const
    {
        struct ListIterable
        {
            typedef Value * iterator;
            iterator _begin, _end;
            iterator begin() const { return _begin; }
            iterator end() const { return _end; }
        };
        assert(isList());
        auto begin = listElems();
        return ListIterable { begin, begin + listSize() };
    }

    SourcePath path() const
    {
        assert(internalType() == tString && untag<const String *>()->isPath());
        return SourcePath{CanonPath(untag<const String *>()->content)};
    }

    std::string_view str() const
    {
        assert(internalType() == tString && !untag<const String *>()->isPath());
        return std::string_view(untag<const String *>()->content);
    }

    NixInt integer() const
    {
        if (internalType() == tInt) {
            intptr_t tmp;
            memcpy(&tmp, &raw, sizeof(tmp));
            return NixInt(tmp >> 3);
        } else {
            assert(internalType() == tAuxiliary && untag<const Acb *>()->type() == Acb::tInt);
            return untag<const Int *>()->value;
        }
    }

    bool boolean() const
    {
        return untag<bool>();
    }

    const auto & string() const
    {
        return *untag<const String *>();
    }

    auto attrs() const
    {
        return untag<Bindings *>();
    }

    Thunk & thunk() const
    {
        return *untag<Thunk *>();
    }

    App & app() const
    {
        return *untag<App *>();
    }

    const auto & lambda() const
    {
        return *untag<const Lambda *>();
    }

    const PrimOp * primOp() const
    {
        assert(internalType() == tAuxiliary && untag<const Acb *>()->type() == Acb::tPrimOp);
        return untag<const PrimOp *>();
    }

    const ExternalValueBase * external() const
    {
        assert(internalType() == tAuxiliary && untag<const Acb *>()->type() == Acb::tExternal);
        return untag<const External *>()->external;
    }

    NixFloat fpoint() const
    {
        assert(internalType() == tAuxiliary && untag<const Acb *>()->type() == Acb::tFloat);
        return untag<const Float *>()->value;
    }

    const Acb * auxiliary() const
    {
        return untag<const Acb *>();
    }

    uintptr_t pointerEqProxy() const
    {
        return raw;
    }
};

struct alignas(Value::TAG_ALIGN) Value::Thunk
{
    union {
        Env * _env;
        Value _result;
    };
    Expr * expr;

    bool resolved() const
    {
        return expr == nullptr;
    }

    void resolve(Value v)
    {
        _result = v;
        expr = nullptr;
    }

    Env * env() const
    {
        return _env;
    }

    Value result() const
    {
        return _result;
    }
};

struct alignas(Value::TAG_ALIGN) Value::List
{
    size_t size;
    Value elems[0];

    std::span<Value> span()
    {
        return {elems, elems + size};
    }
};

struct alignas(Value::TAG_ALIGN) Value::App
{
    Value _left;
    size_t _n;
    Value _args[0];

    bool resolved() const
    {
        return _n == ~size_t(0);
    }

    void resolve(Value v)
    {
        _left = v;
        _n = ~size_t(0);
    }

    Value left() const
    {
        return _left;
    }

    Value result() const
    {
        return left();
    }

    Value target() const
    {
        return left().isApp() ? left().app().target() : left();
    }

    std::span<Value> args()
    {
        return std::span{_args, _n};
    }

    size_t totalArgs() const
    {
        return _n + (left().isApp() ? left().app().totalArgs() : 0);
    }
};

inline ValueType Value::type(bool invalidIsThunk) const
{
again:
    switch (internalType()) {
    case tInt:
        return nInt;
    case tBool:
        return nBool;
    case tString:
        return untag<const String *>()->isPath() ? nPath : nString;
    case tAttrs:
        return nAttrs;
    case tList:
        return nList;
    case tAuxiliary:
        switch (untag<const Acb *>()->type()) {
        case Acb::tExternal:
            return nExternal;
        case Acb::tFloat:
            return nFloat;
        case Acb::tNull:
            return nNull;
        case Acb::tPrimOp:
        case Acb::tLambda:
            return nFunction;
        case Acb::tInt:
            return nInt;
        }
    case tThunk:
        if (isInvalid()) {
            if (invalidIsThunk) {
                return nThunk;
            } else {
                abort();
            }
        } else if (thunk().resolved()) {
            raw = thunk().result().raw;
            goto again;
        }
        return nThunk;
    case tApp:
        if (app().resolved()) {
            raw = app().result().raw;
            goto again;
        }
        return app().target().isPrimOp() ? nFunction : nThunk;
    }
}

inline bool Value::isBlackhole() const
{
    return internalType() == tThunk && untag<const Thunk *>()->expr == blackHole.expr;
}

inline bool Value::isPrimOpApp() const
{
    return internalType() == tApp && !app().resolved() && app().target().isPrimOp();
}

inline Value * Value::listElems() const
{
    return untag<List *>()->elems;
}

inline size_t Value::listSize() const
{
    return untag<const List *>()->size;
}

using PrimOp = Value::PrimOp;

/**
 * A value allocated in traceable memory.
 */
typedef std::shared_ptr<Value> RootValue;

RootValue allocRootValue(Value v);
}
