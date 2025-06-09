#pragma once
///@file

#include <cassert>
#include <climits>
#include <functional>
#include <ranges>
#include <span>

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


typedef enum {
    tInt = 1,
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList1,
    tList2,
    tListN,
    tThunk,
    tApp,
    tLambda,
    tPrimOp,
    tPrimOpApp,
    tExternal,
    tFloat
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
struct PrimOp;
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

/** This is just the address of eBlackHole. It exists because eBlackHole has an
 * incomplete type at usage sites so is not possible to cast. */
extern Expr *eBlackHoleAddr;

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

    struct primOpApp_t { };
    constexpr static primOpApp_t primOpApp{};

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
    InternalType internalType;

    friend std::string showType(const Value & v);

public:

    /**
     * Empty list constant.
     */
    static Value EMPTY_LIST;

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
    USING_VALUETYPE(primOpApp_t);
    USING_VALUETYPE(lambda_t);
    USING_VALUETYPE(external_t);
    USING_VALUETYPE(blackhole_t);
#undef USING_VALUETYPE

    /// Default constructor which is still used in the codebase but should not
    /// be used in new code. Zero initializes its members.
    [[deprecated]] Value()
        : internalType(static_cast<InternalType>(0))
        , _empty{ 0, 0 }
    { }

    /// Constructs a nix language value of type "int", with the integral value
    /// of @ref i.
    Value(integer_t, NixInt i)
        : internalType(tInt)
        , _empty{ 0, 0 }
    {
        // the NixInt ctor here is is special because NixInt has a ctor too, so
        // we're not allowed to have it as an anonymous aggreagte member. we do
        // however still have the option to clear the data members using _empty
        // and leaving the second word of data cleared by setting only integer.
        integer = i;
    }

    /// Constructs a nix language value of type "float", with the floating
    /// point value of @ref f.
    Value(floating_t, NixFloat f)
        : internalType(tFloat)
        , fpoint(f)
        , _float_pad(0)
    { }

    /// Constructs a nix language value of type "bool", with the boolean
    /// value of @ref b.
    Value(boolean_t, bool b)
        : internalType(tBool)
        , boolean(b)
        , _bool_pad(0)
    { }

    /// Constructs a nix language value of type "string", with the value of the
    /// C-string pointed to by @ref strPtr, and optionally with an array of
    /// string context pointed to by @ref contextPtr.
    ///
    /// Neither the C-string nor the context array are copied; this constructor
    /// assumes suitable memory has already been allocated (with the GC if
    /// enabled), and string and context data copied into that memory.
    Value(string_t, char const * strPtr, char const ** contextPtr = nullptr)
        : internalType(tString)
        , string({ .s = strPtr, .context = contextPtr })
    { }

    /// Constructx a nix language value of type "string", with a copy of the
    /// string data viewed by @ref copyFrom.
    ///
    /// The string data *is* copied from @ref copyFrom, and this constructor
    /// performs a dynamic (GC) allocation to do so.
    Value(string_t, std::string_view copyFrom, NixStringContext const & context = {})
        : internalType(tString)
        , string({ .s = gcCopyStringIfNeeded(copyFrom), .context = nullptr })
    {
        if (context.empty()) {
            // It stays nullptr.
            return;
        }

        // Copy the context.
        this->string.context = gcAllocType<char const *>(context.size() + 1);

        size_t n = 0;
        for (NixStringContextElem const & contextElem : context) {
            this->string.context[n] = gcCopyStringIfNeeded(contextElem.to_string());
            n += 1;
        }

        // Terminator sentinel.
        this->string.context[n] = nullptr;
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
        : internalType(tString)
        , string({ .s = strPtr, .context = nullptr })
    {
        if (context.empty()) {
            // It stays nullptr
            return;
        }

        // Copy the context.
        this->string.context = gcAllocType<char const *>(context.size() + 1);

        size_t n = 0;
        for (NixStringContextElem const & contextElem : context) {
            this->string.context[n] = gcCopyStringIfNeeded(contextElem.to_string());
            n += 1;
        }

        // Terminator sentinel.
        this->string.context[n] = nullptr;
    }

    /// Constructs a nix language value of type "path", with the value of the
    /// C-string pointed to by @ref strPtr.
    ///
    /// The C-string is not copied; this constructor assumes suitable memory
    /// has already been allocated (with the GC if enabled), and string data
    /// has been copied into that memory.
    Value(path_t, char const * strPtr)
        : internalType(tPath)
        , _path(strPtr)
        , _path_pad(0)
    { }

    /// Constructs a nix language value of type "path", with the path
    /// @ref path.
    ///
    /// The data from @ref path *is* copied, and this constructor performs a
    /// dynamic (GC) allocation to do so.
    Value(path_t, SourcePath const & path)
        : internalType(tPath)
        , _path(gcCopyStringIfNeeded(path.canonical().abs()))
        , _path_pad(0)
    { }

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
    Value(list_t, std::span<Value *> items)
    {
        if (items.size() == 1) {
            this->internalType = tList1;
            this->smallList[0] = items[0];
            this->smallList[1] = nullptr;
        } else if (items.size() == 2) {
            this->internalType = tList2;
            this->smallList[0] = items[0];
            this->smallList[1] = items[1];
        } else {
            this->internalType = tListN;
            this->bigList.size = items.size();
            this->bigList.elems = items.data();
        }
    }

    /// Constructs a nix language value of type "list", with an element array
    /// initialized by applying @ref transformer to each element in @ref items.
    ///
    /// This allows "in-place" construction of a nix list when some logic is
    /// needed to get each Value pointer. This constructor dynamically (GC)
    /// allocates memory for the size of @ref items, and the Value pointers
    /// returned by @ref transformer are shallow copied into it.
    template<
        std::ranges::sized_range SizedIterableT,
        InvocableR<Value *, typename SizedIterableT::value_type const &> TransformerT
    >
    Value(list_t, SizedIterableT & items, TransformerT const & transformer)
    {
        if (items.size() == 1) {
            this->internalType = tList1;
            this->smallList[0] = transformer(*items.begin());
            this->smallList[1] = nullptr;
        } else if (items.size() == 2) {
            this->internalType = tList2;
            auto it = items.begin();
            this->smallList[0] = transformer(*it);
            it++;
            this->smallList[1] = transformer(*it);
        } else {
            this->internalType = tListN;
            this->bigList.size = items.size();
            this->bigList.elems = gcAllocType<Value *>(items.size());
            auto it = items.begin();
            for (size_t i = 0; i < items.size(); i++, it++) {
                this->bigList.elems[i] = transformer(*it);
            }
        }
    }

    /// Constructs a nix language value of the singleton type "null".
    Value(null_t)
        : internalType(tNull)
        , _empty{0, 0}
    { }

    /// Constructs a nix language value of type "set", with the attribute
    /// bindings pointed to by @ref bindings.
    ///
    /// The bindings are not not copied; this constructor assumes @ref bindings
    /// has already been suitably allocated by something like nix::buildBindings.
    Value(attrs_t, Bindings * bindings)
        : internalType(tAttrs)
        , attrs(bindings)
        , _attrs_pad(0)
    { }

    /// Constructs a nix language lazy delayed computation, or "thunk".
    ///
    /// The thunk stores the environment it will be computed in @ref env, and
    /// the expression that will need to be evaluated @ref expr.
    Value(thunk_t, Env & env, Expr & expr)
        : internalType(tThunk)
        , thunk({ .env = &env, .expr = &expr })
    { }

    /// Constructs a nix language value of type "lambda", which represents
    /// a builtin, primitive operation ("primop"), from the primop
    /// implemented by @ref primop.
    Value(primop_t, PrimOp & primop);

    /// Constructs a nix language value of type "lambda", which represents a
    /// partially applied primop.
    Value(primOpApp_t, Value & lhs, Value & rhs)
        : internalType(tPrimOpApp)
        , primOpApp({ .left = &lhs, .right = &rhs })
    { }

    /// Constructs a nix language value of type "lambda", which represents a
    /// lazy partial application of another lambda.
    Value(app_t, Value & lhs, Value & rhs)
        : internalType(tApp)
        , app({ .left = &lhs, .right = &rhs })
    { }

    /// Constructs a nix language value of type "external", which is only used
    /// by plugins. Do any existing plugins even use this mechanism?
    Value(external_t, ExternalValueBase & external)
        : internalType(tExternal)
        , external(&external)
        , _external_pad(0)
    { }

    /// Constructs a nix language value of type "lambda", which represents a
    /// run of the mill lambda defined in nix code.
    ///
    /// This takes the environment the lambda is closed over @ref env, and
    /// the lambda expression itself @ref lambda, which will not be evaluated
    /// until it is applied.
    Value(lambda_t, Env & env, ExprLambda & lambda)
        : internalType(tLambda)
        , lambda({ .env = &env, .fun = &lambda })
    { }

    /// Constructs an evil thunk, whose evaluation represents infinite recursion.
    explicit Value(blackhole_t)
        : internalType(tThunk)
        , thunk({ .env = nullptr, .expr = eBlackHoleAddr })
    { }

    Value(Value const & rhs) = default;

    /// Move constructor. Does the same thing as the copy constructor, but
    /// also zeroes out the other Value.
    Value(Value && rhs)
        : internalType(rhs.internalType)
        , _empty{ 0, 0 }
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
            rhs.internalType = static_cast<InternalType>(0);
            rhs._empty[0] = 0;
            rhs._empty[1] = 0;
        }
        return *this;
    }

    void print(EvalState &state, std::ostream &str, PrintOptions options = PrintOptions {});

    // Functions needed to distinguish the type
    // These should be removed eventually, by putting the functionality that's
    // needed by callers into methods of this type

    // type() == nThunk
    inline bool isThunk() const { return internalType == tThunk; };
    inline bool isApp() const { return internalType == tApp; };
    inline bool isBlackhole() const
    {
        return internalType == tThunk && thunk.expr == eBlackHoleAddr;
    }

    // type() == nFunction
    inline bool isLambda() const { return internalType == tLambda; };
    inline bool isPrimOp() const { return internalType == tPrimOp; };
    inline bool isPrimOpApp() const { return internalType == tPrimOpApp; };

    union
    {
        /// Dummy field, which takes up as much space as the largest union variants
        /// to set the union's memory to zeroed memory.
        uintptr_t _empty[2];

        NixInt integer;
        struct {
            bool boolean;
            uintptr_t _bool_pad;
        };

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
        struct {
            const char * s;
            const char * * context; // must be in sorted order
        } string;

        struct {
            const char * _path;
            uintptr_t _path_pad;
        };
        struct {
            Bindings * attrs;
            uintptr_t _attrs_pad;
        };
        struct {
            size_t size;
            Value * * elems;
        } bigList;
        Value * smallList[2];
        struct {
            Env * env;
            Expr * expr;
        } thunk;
        struct {
            Value * left, * right;
        } app;
        struct {
            Env * env;
            ExprLambda * fun;
        } lambda;
        struct {
            PrimOp * primOp;
            uintptr_t _primop_pad;
        };
        struct {
            Value * left, * right;
        } primOpApp;
        struct {
            ExternalValueBase * external;
            uintptr_t _external_pad;
        };
        struct {
            NixFloat fpoint;
            uintptr_t _float_pad;
        };
    };

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     *
     * @param invalidIsThunk Instead of aborting an an invalid (probably
     * 0, so uninitialized) internal type, return `nThunk`.
     */
    inline ValueType type(bool invalidIsThunk = false) const
    {
        switch (internalType) {
            case tInt: return nInt;
            case tBool: return nBool;
            case tString: return nString;
            case tPath: return nPath;
            case tNull: return nNull;
            case tAttrs: return nAttrs;
            case tList1: case tList2: case tListN: return nList;
            case tLambda: case tPrimOp: case tPrimOpApp: return nFunction;
            case tExternal: return nExternal;
            case tFloat: return nFloat;
            case tThunk: case tApp: return nThunk;
        }
        if (invalidIsThunk)
            return nThunk;
        else
            abort();
    }

    /**
     * After overwriting an app node, be sure to clear pointers in the
     * Value to ensure that the target isn't kept alive unnecessarily.
     */
    inline void clearValue()
    {
        app.left = app.right = 0;
    }

    inline void mkInt(NixInt::Inner n)
    {
        mkInt(NixInt{n});
    }

    inline void mkInt(NixInt n)
    {
        clearValue();
        internalType = tInt;
        integer = n;
    }

    inline void mkBool(bool b)
    {
        clearValue();
        internalType = tBool;
        boolean = b;
    }

    inline void mkString(const char * s, const char * * context = 0)
    {
        internalType = tString;
        string.s = s;
        string.context = context;
    }

    void mkString(std::string_view s);

    void mkString(std::string_view s, const NixStringContext & context);

    void mkStringMove(const char * s, const NixStringContext & context);

    void mkPath(const SourcePath & path);

    inline void mkPath(const char * path)
    {
        clearValue();
        internalType = tPath;
        _path = path;
    }

    inline void mkNull()
    {
        clearValue();
        internalType = tNull;
    }

    inline void mkAttrs(Bindings * a)
    {
        clearValue();
        internalType = tAttrs;
        attrs = a;
    }

    Value & mkAttrs(BindingsBuilder & bindings);

    inline void mkList(size_t size)
    {
        clearValue();
        if (size == 1)
            internalType = tList1;
        else if (size == 2)
            internalType = tList2;
        else {
            internalType = tListN;
            bigList.size = size;
        }
    }

    inline void mkThunk(Env * e, Expr & ex)
    {
        internalType = tThunk;
        thunk.env = e;
        thunk.expr = &ex;
    }

    inline void mkApp(Value * l, Value * r)
    {
        internalType = tApp;
        app.left = l;
        app.right = r;
    }

    inline void mkLambda(Env * e, ExprLambda * f)
    {
        internalType = tLambda;
        lambda.env = e;
        lambda.fun = f;
    }

    inline void mkBlackhole()
    {
        internalType = tThunk;
        thunk.expr = eBlackHoleAddr;
    }

    void mkPrimOp(PrimOp * p);

    inline void mkPrimOpApp(Value * l, Value * r)
    {
        internalType = tPrimOpApp;
        primOpApp.left = l;
        primOpApp.right = r;
    }

    /**
     * For a `tPrimOpApp` value, get the original `PrimOp` value.
     */
    PrimOp * primOpAppPrimOp() const;

    inline void mkExternal(ExternalValueBase * e)
    {
        clearValue();
        internalType = tExternal;
        external = e;
    }

    inline void mkFloat(NixFloat n)
    {
        clearValue();
        internalType = tFloat;
        fpoint = n;
    }

    bool isList() const
    {
        return internalType == tList1 || internalType == tList2 || internalType == tListN;
    }

    Value * * listElems()
    {
        return internalType == tList1 || internalType == tList2 ? smallList : bigList.elems;
    }

    Value * const * listElems() const
    {
        return internalType == tList1 || internalType == tList2 ? smallList : bigList.elems;
    }

    size_t listSize() const
    {
        return internalType == tList1 ? 1 : internalType == tList2 ? 2 : bigList.size;
    }

    /**
     * Check whether forcing this value requires a trivial amount of
     * computation. In particular, function applications are
     * non-trivial.
     */
    bool isTrivial() const;

    auto listItems()
    {
        struct ListIterable
        {
            typedef Value * const * iterator;
            iterator _begin, _end;
            iterator begin() const { return _begin; }
            iterator end() const { return _end; }
        };
        assert(isList());
        auto begin = listElems();
        return ListIterable { begin, begin + listSize() };
    }

    auto listItems() const
    {
        struct ConstListIterable
        {
            typedef const Value * const * iterator;
            iterator _begin, _end;
            iterator begin() const { return _begin; }
            iterator end() const { return _end; }
        };
        assert(isList());
        auto begin = listElems();
        return ConstListIterable { begin, begin + listSize() };
    }

    SourcePath path() const
    {
        assert(internalType == tPath);
        return SourcePath{CanonPath(_path)};
    }

    std::string_view str() const
    {
        assert(internalType == tString);
        return std::string_view(string.s);
    }
};

using ValueVector = GcVector<Value *>;

/**
 * A value allocated in traceable memory.
 */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

}
