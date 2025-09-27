#include "lix/libexpr/eval-cache.hh"
#include "lix/libstore/sqlite.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/users.hh"

namespace nix::eval_cache {

static const char * schema = R"sql(
create table if not exists Attributes (
    parent      integer not null,
    name        text,
    type        integer not null,
    value       text,
    context     text,
    primary key (parent, name)
);
)sql";

struct AttrDb
{
    std::atomic_bool failed{false};

    struct State
    {
        SQLite db;
        SQLiteStmt insertAttribute;
        SQLiteStmt insertAttributeWithContext;
        SQLiteStmt queryAttribute;
        SQLiteStmt queryAttributes;
        std::unique_ptr<SQLiteTxn> txn;
    };

    std::unique_ptr<Sync<State>> _state;

    AttrDb(const Hash & fingerprint)
        : _state(std::make_unique<Sync<State>>())
    {
        auto state(_state->lock());

        Path cacheDir = getCacheDir() + "/nix/eval-cache-v5";
        createDirs(cacheDir);

        Path dbPath = cacheDir + "/" + fingerprint.to_string(Base::Base16, false) + ".sqlite";

        state->db = SQLite(dbPath);
        state->db.isCache();
        state->db.exec(schema, always_progresses);

        state->insertAttribute = state->db.create(
            "insert or replace into Attributes(parent, name, type, value) values (?, ?, ?, ?)");

        state->insertAttributeWithContext = state->db.create(
            "insert or replace into Attributes(parent, name, type, value, context) values (?, ?, ?, ?, ?)");

        state->queryAttribute = state->db.create(
            "select rowid, type, value, context from Attributes where parent = ? and name = ?");

        state->queryAttributes = state->db.create(
            "select name from Attributes where parent = ?");

        state->txn = std::make_unique<SQLiteTxn>(state->db.beginTransaction());
    }

    ~AttrDb()
    {
        try {
            auto state(_state->lock());
            if (!failed)
                state->txn->commit();
            state->txn.reset();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

    template<typename F>
    AttrId doSQLite(F && fun)
    {
        if (failed) return 0;
        try {
            return fun();
        } catch (SQLiteError &) {
            ignoreExceptionExceptInterrupt();
            failed = true;
            return 0;
        }
    }

    AttrId setAttrs(
        AttrKey key,
        const fullattr_t & attrs)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::FullAttrs)
                (0, false).exec();

            AttrId rowId = state->db.getLastInsertedRowId();
            assert(rowId);

            for (auto & attr : attrs.p)
                state->insertAttribute.use()
                    (rowId)
                    (attr)
                    (AttrType::Placeholder)
                    (0, false).exec();

            return rowId;
        });
    }

    AttrId setString(
        AttrKey key,
        std::string_view s,
        const char * * context = nullptr)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            if (context) {
                std::string ctx;
                for (const char * * p = context; *p; ++p) {
                    if (p != context) ctx.push_back(' ');
                    ctx.append(*p);
                }
                state->insertAttributeWithContext.use()
                    (key.first)
                    (key.second)
                    (AttrType::String)
                    (s)
                    (ctx).exec();
            } else {
                state->insertAttribute.use()
                    (key.first)
                    (key.second)
                    (AttrType::String)
                (s).exec();
            }

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setBool(
        AttrKey key,
        bool b)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Bool)
                (b ? 1 : 0).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setInt(
        AttrKey key,
        int n)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Int)
                (n).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setListOfStrings(
        AttrKey key,
        const std::vector<std::string> & l)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::ListOfStrings)
                (concatStringsSep("\t", l)).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setPlaceholder(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Placeholder)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setMissing(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Missing)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setMisc(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Misc)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    AttrId setFailed(AttrKey key)
    {
        return doSQLite([&]()
        {
            auto state(_state->lock());

            state->insertAttribute.use()
                (key.first)
                (key.second)
                (AttrType::Failed)
                (0, false).exec();

            return state->db.getLastInsertedRowId();
        });
    }

    std::optional<std::pair<AttrId, AttrValue>> getAttr(AttrKey key)
    {
        auto state(_state->lock());

        auto queryAttribute(state->queryAttribute.use()(key.first)(key.second));
        if (!queryAttribute.next()) return {};

        auto rowId = (AttrId) queryAttribute.getInt(0);
        auto type = (AttrType) queryAttribute.getInt(1);

        switch (type) {
            case AttrType::Placeholder:
                return {{rowId, placeholder_t()}};
            case AttrType::FullAttrs: {
                // FIXME: expensive, should separate this out.
                fullattr_t attrs;
                auto queryAttributes(state->queryAttributes.use()(rowId));
                while (queryAttributes.next())
                    attrs.p.emplace_back(queryAttributes.getStr(0));
                return {{rowId, attrs}};
            }
            case AttrType::String: {
                NixStringContext context;
                if (!queryAttribute.isNull(3))
                    for (auto & s : tokenizeString<std::vector<std::string>>(queryAttribute.getStr(3), ";"))
                        context.insert(NixStringContextElem::parse(s));
                return {{rowId, string_t{queryAttribute.getStr(2), context}}};
            }
            case AttrType::Bool:
                return {{rowId, queryAttribute.getInt(2) != 0}};
            case AttrType::Int:
                return {{rowId, int_t{NixInt{queryAttribute.getInt(2)}}}};
            case AttrType::ListOfStrings:
                return {{rowId, tokenizeString<std::vector<std::string>>(queryAttribute.getStr(2), "\t")}};
            case AttrType::Missing:
                return {{rowId, missing_t()}};
            case AttrType::Misc:
                return {{rowId, misc_t()}};
            case AttrType::Failed:
                return {{rowId, failed_t()}};
            default:
                throw Error("unexpected type in evaluation cache");
        }
    }
};

static std::shared_ptr<AttrDb> makeAttrDb(const Hash & fingerprint)
{
    try {
        return std::make_shared<AttrDb>(fingerprint);
    } catch (SQLiteError &) {
        ignoreExceptionExceptInterrupt();
        return nullptr;
    }
}

ref<EvalCache> CachingEvaluator::getCacheFor(Hash hash, RootLoader rootLoader)
{
    if (auto it = caches.find(hash); it != caches.end()) {
        return it->second;
    }
    auto cache = make_ref<EvalCache>(hash, rootLoader);
    caches.emplace(hash, cache);
    return cache;
}

EvalCache::EvalCache(
    std::optional<std::reference_wrapper<const Hash>> useCache,
    RootLoader rootLoader)
    : db(useCache ? makeAttrDb(*useCache) : nullptr)
    , rootLoader(rootLoader)
{
}

Value & EvalCache::getRootValue(EvalState & state)
{
    if (!value) {
        debug("getting root value");
        value = allocRootValue(rootLoader(state));
    }
    return *value;
}

ref<AttrCursor> EvalCache::getRoot()
{
    return make_ref<AttrCursor>(ref<EvalCache>(*this), std::nullopt);
}

AttrCursor::AttrCursor(
    ref<EvalCache> root,
    Parent parent,
    Value * value,
    std::optional<std::pair<AttrId, AttrValue>> && cachedValue)
    : root(root), parent(parent), cachedValue(std::move(cachedValue))
{
    if (value) {
        _value = allocRootValue(*value);
    }
}

AttrKey AttrCursor::getKey()
{
    if (!parent)
        return {0, ""};
    if (!parent->first->cachedValue) {
        parent->first->cachedValue = root->db->getAttr(parent->first->getKey());
        assert(parent->first->cachedValue);
    }
    return {parent->first->cachedValue->first, parent->second};
}

Value & AttrCursor::getValue(EvalState & state)
{
    if (!_value) {
        if (parent) {
            auto & vParent = parent->first->getValue(state);
            state.forceAttrs(vParent, noPos, "while searching for an attribute");
            auto attr = vParent.attrs()->get(state.ctx.symbols.create(parent->second));
            if (!attr)
                throw Error("attribute '%s' is unexpectedly missing", getAttrPathStr(state));
            _value = allocRootValue(attr->value);
        } else
            _value = allocRootValue(root->getRootValue(state));
    }
    return *_value;
}

std::vector<std::string> AttrCursor::getAttrPath(EvalState & state) const
{
    if (parent) {
        auto attrPath = parent->first->getAttrPath(state);
        attrPath.push_back(parent->second);
        return attrPath;
    } else
        return {};
}

std::vector<std::string> AttrCursor::getAttrPath(EvalState & state, std::string_view name) const
{
    auto attrPath = getAttrPath(state);
    attrPath.emplace_back(name);
    return attrPath;
}

std::string AttrCursor::getAttrPathStr(EvalState & state) const
{
    return concatStringsSep(".", getAttrPath(state));
}

std::string AttrCursor::getAttrPathStr(EvalState & state, std::string_view name) const
{
    return concatStringsSep(".", getAttrPath(state, name));
}

Value & AttrCursor::forceValue(EvalState & state)
{
    debug("evaluating uncached attribute '%s'", getAttrPathStr(state));

    auto & v = getValue(state);

    try {
        state.forceValue(v, noPos);
    } catch (EvalError &) {
        debug("setting '%s' to failed", getAttrPathStr(state));
        if (root->db)
            cachedValue = {root->db->setFailed(getKey()), failed_t()};
        throw;
    }

    if (root->db && (!cachedValue || std::get_if<placeholder_t>(&cachedValue->second))) {
        if (v.type() == nString)
            cachedValue = {
                root->db->setString(getKey(), v.str(), v.string().context), string_t{v.str(), {}}
            };
        else if (v.type() == nPath) {
            auto path = v.path().canonical().abs();
            cachedValue = {root->db->setString(getKey(), path), string_t{path, {}}};
        }
        else if (v.type() == nBool)
            cachedValue = {root->db->setBool(getKey(), v.boolean()), v.boolean()};
        else if (v.type() == nInt)
            cachedValue = {root->db->setInt(getKey(), v.integer().value), int_t{v.integer()}};
        else if (v.type() == nAttrs)
            ; // FIXME: do something?
        else
            cachedValue = {root->db->setMisc(getKey()), misc_t()};
    }

    return v;
}

Suggestions AttrCursor::getSuggestionsForAttr(EvalState & state, const std::string & name)
{
    auto attrNames = getAttrs(state);
    return Suggestions::bestMatches({attrNames.begin(), attrNames.end()}, name);
}

std::shared_ptr<AttrCursor> AttrCursor::maybeGetAttr(EvalState & state, const std::string & name)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());

        if (cachedValue) {
            if (auto attrs = std::get_if<fullattr_t>(&cachedValue->second)) {
                for (auto & attr : attrs->p)
                    if (attr == name)
                        return std::make_shared<AttrCursor>(root, std::make_pair(shared_from_this(), name));
                return nullptr;
            } else if (std::get_if<placeholder_t>(&cachedValue->second)) {
                auto attr = root->db->getAttr({cachedValue->first, name});
                if (attr) {
                    if (std::get_if<missing_t>(&attr->second))
                        return nullptr;
                    else if (std::get_if<failed_t>(&attr->second)) {
                        debug("reevaluating failed cached attribute '%s'", getAttrPathStr(state, name));
                    } else
                        return std::make_shared<AttrCursor>(root,
                            std::make_pair(shared_from_this(), name), nullptr, std::move(attr));
                }
                // Incomplete attrset, so need to fall thru and
                // evaluate to see whether 'name' exists
            } else
                return nullptr;
                //errors.make<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() != nAttrs)
        return nullptr;
        //errors.make<TypeError>("'%s' is not an attribute set", getAttrPathStr()).debugThrow();

    auto attr = v.attrs()->get(state.ctx.symbols.create(name));

    if (!attr) {
        if (root->db) {
            if (!cachedValue)
                cachedValue = {root->db->setPlaceholder(getKey()), placeholder_t()};
            root->db->setMissing({cachedValue->first, name});
        }
        return nullptr;
    }

    std::optional<std::pair<AttrId, AttrValue>> cachedValue2;
    if (root->db) {
        if (!cachedValue)
            cachedValue = {root->db->setPlaceholder(getKey()), placeholder_t()};
        cachedValue2 = {root->db->setPlaceholder({cachedValue->first, name}), placeholder_t()};
    }

    return make_ref<AttrCursor>(
        root, std::make_pair(shared_from_this(), name), &attr->value, std::move(cachedValue2)
    );
}

ref<AttrCursor> AttrCursor::getAttr(EvalState & state, const std::string & name)
{
    auto p = maybeGetAttr(state, name);
    if (!p)
        throw Error("attribute '%s' does not exist", getAttrPathStr(state, name));
    return ref<AttrCursor>::unsafeFromPtr(p);
}

OrSuggestions<ref<AttrCursor>> AttrCursor::findAlongAttrPath(EvalState & state, const std::vector<std::string> & attrPath)
{
    auto res = shared_from_this();
    for (auto & attr : attrPath) {
        auto child = res->maybeGetAttr(state, attr);
        if (!child) {
            auto suggestions = res->getSuggestionsForAttr(state, attr);
            return OrSuggestions<ref<AttrCursor>>::failed(suggestions);
        }
        res = child;
    }
    return ref<AttrCursor>::unsafeFromPtr(res);
}

std::string AttrCursor::getString(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                debug("using cached string attribute '%s'", getAttrPathStr(state));
                return s->first;
            } else
                state.ctx.errors.make<TypeError>("'%s' is not a string", getAttrPathStr(state)).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() != nString && v.type() != nPath) {
        state.ctx.errors.make<TypeError>("'%s' is not a string but %s", getAttrPathStr(state), v.type()).debugThrow();
    }

    return v.type() == nString ? std::string(v.str()) : v.path().to_string();
}

string_t AttrCursor::getStringWithContext(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto s = std::get_if<string_t>(&cachedValue->second)) {
                bool valid = true;
                for (auto & c : s->second) {
                    const StorePath & path = std::visit(overloaded {
                        [&](const NixStringContextElem::DrvDeep & d) -> const StorePath & {
                            return d.drvPath;
                        },
                        [&](const NixStringContextElem::Built & b) -> const StorePath & {
                            return b.drvPath.path;
                        },
                        [&](const NixStringContextElem::Opaque & o) -> const StorePath & {
                            return o.path;
                        },
                    }, c.raw);
                    if (!state.aio.blockOn(state.ctx.store->isValidPath(path))) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    debug("using cached string attribute '%s'", getAttrPathStr(state));
                    return *s;
                }
            } else
                state.ctx.errors.make<TypeError>("'%s' is not a string", getAttrPathStr(state)).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() == nString) {
        NixStringContext context;
        copyContext(v, context);
        return {std::string(v.str()), std::move(context)};
    } else if (v.type() == nPath) {
        return {v.path().to_string(), {}};
    } else {
        state.ctx.errors.make<TypeError>("'%s' is not a string but %s", getAttrPathStr(state), v.type()).debugThrow();
    }
}

bool AttrCursor::getBool(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto b = std::get_if<bool>(&cachedValue->second)) {
                debug("using cached Boolean attribute '%s'", getAttrPathStr(state));
                return *b;
            } else
                state.ctx.errors.make<TypeError>("'%s' is not a Boolean", getAttrPathStr(state)).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() != nBool)
        state.ctx.errors.make<TypeError>("'%s' is not a Boolean", getAttrPathStr(state)).debugThrow();

    return v.boolean();
}

NixInt AttrCursor::getInt(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto i = std::get_if<int_t>(&cachedValue->second)) {
                debug("using cached integer attribute '%s'", getAttrPathStr(state));
                return i->x;
            } else
                state.ctx.errors.make<TypeError>("'%s' is not an integer", getAttrPathStr(state)).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() != nInt)
        state.ctx.errors.make<TypeError>("'%s' is not an integer", getAttrPathStr(state)).debugThrow();

    return v.integer();
}

std::vector<std::string> AttrCursor::getListOfStrings(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto l = std::get_if<std::vector<std::string>>(&cachedValue->second)) {
                debug("using cached list of strings attribute '%s'", getAttrPathStr(state));
                return *l;
            } else
                state.ctx.errors.make<TypeError>("'%s' is not a list of strings", getAttrPathStr(state)).debugThrow();
        }
    }

    debug("evaluating uncached attribute '%s'", getAttrPathStr(state));

    auto & v = getValue(state);
    state.forceValue(v, noPos);

    if (v.type() != nList)
        state.ctx.errors.make<TypeError>("'%s' is not a list", getAttrPathStr(state)).debugThrow();

    std::vector<std::string> res;

    for (auto & elem : v.listItems()) {
        res.push_back(std::string(
            state.forceStringNoCtx(elem, noPos, "while evaluating an attribute for caching")
        ));
    }

    if (root->db) {
        cachedValue = {root->db->setListOfStrings(getKey(), res), res};
    }

    return res;
}

std::vector<std::string> AttrCursor::getAttrs(EvalState & state)
{
    if (root->db) {
        if (!cachedValue)
            cachedValue = root->db->getAttr(getKey());
        if (cachedValue && !std::get_if<placeholder_t>(&cachedValue->second)) {
            if (auto attrs = std::get_if<fullattr_t>(&cachedValue->second)) {
                debug("using cached attrset attribute '%s'", getAttrPathStr(state));
                return attrs->p;
            } else
                state.ctx.errors.make<TypeError>("'%s' is not an attribute set", getAttrPathStr(state)).debugThrow();
        }
    }

    auto & v = forceValue(state);

    if (v.type() != nAttrs)
        state.ctx.errors.make<TypeError>("'%s' is not an attribute set", getAttrPathStr(state)).debugThrow();

    fullattr_t attrs;
    for (auto & attr : *getValue(state).attrs())
        attrs.p.emplace_back(state.ctx.symbols[attr.name]);
    std::sort(attrs.p.begin(), attrs.p.end());

    if (root->db)
        cachedValue = {root->db->setAttrs(getKey(), attrs), attrs};

    return attrs.p;
}

bool AttrCursor::isDerivation(EvalState & state)
{
    auto aType = maybeGetAttr(state, "type");
    return aType && aType->getString(state) == "derivation";
}

StorePath AttrCursor::forceDerivation(EvalState & state)
{
    auto aDrvPath = getAttr(state, "drvPath");
    auto drvPath = state.ctx.store->parseStorePath(aDrvPath->getString(state));
    if (!state.aio.blockOn(state.ctx.store->isValidPath(drvPath)) && !settings.readOnlyMode) {
        /* The eval cache contains 'drvPath', but the actual path has
           been garbage-collected. So force it to be regenerated. */
        aDrvPath->forceValue(state);
        if (!state.aio.blockOn(state.ctx.store->isValidPath(drvPath)))
            throw Error("don't know how to recreate store derivation '%s'!",
                state.ctx.store->printStorePath(drvPath));
    }
    return drvPath;
}

}
