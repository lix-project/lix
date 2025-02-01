#include "lix/libexpr/get-drvs.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/path-with-outputs.hh"

#include <cstring>
#include <regex>


namespace nix {


DrvInfo::DrvInfo(std::string attrPath, Bindings * attrs)
    : attrs(attrs), attrPath(std::move(attrPath))
{
}


DrvInfo::DrvInfo(ref<Store> store, const std::string & drvPathWithOutputs)
    : attrs(nullptr), attrPath("")
{
    auto [drvPath, selectedOutputs] = parsePathWithOutputs(*store, drvPathWithOutputs);

    this->drvPath = drvPath;

    auto drv = RUN_ASYNC_IN_NEW_THREAD(store->derivationFromPath(drvPath));

    name = drvPath.name();

    if (selectedOutputs.size() > 1)
        throw Error("building more than one derivation output is not supported, in '%s'", drvPathWithOutputs);

    outputName =
        selectedOutputs.empty()
        ? getOr(drv.env, "outputName", "out")
        : *selectedOutputs.begin();

    auto i = drv.outputs.find(outputName);
    if (i == drv.outputs.end())
        throw Error("derivation '%s' does not have output '%s'", store->printStorePath(drvPath), outputName);
    auto & [outputName, output] = *i;

    outPath = {output.path(*store, drv.name, outputName)};
}


std::string DrvInfo::queryName(EvalState & state)
{
    if (name == "" && attrs) {
        auto i = attrs->find(state.ctx.s.name);
        if (i == attrs->end()) state.ctx.errors.make<TypeError>("derivation name missing").debugThrow();
        name = state.forceStringNoCtx(*i->value, noPos, "while evaluating the 'name' attribute of a derivation");
    }
    return name;
}


std::string DrvInfo::querySystem(EvalState & state)
{
    if (system == "" && attrs) {
        auto i = attrs->find(state.ctx.s.system);
        system = i == attrs->end() ? "unknown" : state.forceStringNoCtx(*i->value, i->pos, "while evaluating the 'system' attribute of a derivation");
    }
    return system;
}


std::optional<StorePath> DrvInfo::queryDrvPath(EvalState & state)
{
    if (!drvPath && attrs) {
        Bindings::iterator i = attrs->find(state.ctx.s.drvPath);
        NixStringContext context;
        if (i == attrs->end())
            drvPath = {std::nullopt};
        else
            drvPath = {state.coerceToStorePath(i->pos, *i->value, context, "while evaluating the 'drvPath' attribute of a derivation")};
    }
    return drvPath.value_or(std::nullopt);
}


StorePath DrvInfo::requireDrvPath(EvalState & state)
{
    if (auto drvPath = queryDrvPath(state))
        return *drvPath;
    throw Error("derivation does not contain a 'drvPath' attribute");
}


StorePath DrvInfo::queryOutPath(EvalState & state)
{
    if (!outPath && attrs) {
        Bindings::iterator i = attrs->find(state.ctx.s.outPath);
        NixStringContext context;
        if (i != attrs->end())
            outPath = state.coerceToStorePath(i->pos, *i->value, context, "while evaluating the output path of a derivation");
    }
    if (!outPath)
        throw UnimplementedError("CA derivations are not yet supported");
    return *outPath;
}

void DrvInfo::fillOutputs(EvalState & state, bool withPaths)
{
    auto fillDefault = [&]() {
        std::optional<StorePath> outPath = std::nullopt;
        if (withPaths) {
            outPath.emplace(this->queryOutPath(state));
        }
        this->outputs.emplace("out", outPath);
    };

    // lol. lmao even.
    if (this->attrs == nullptr) {
        fillDefault();
        return;
    }

    Attr * outputs = this->attrs->get(state.ctx.s.outputs);
    if (outputs == nullptr) {
        fillDefault();
        return;
    }

    // NOTE(Qyriad): I don't think there is any codepath that can cause this to error.
    state.forceList(
        *outputs->value,
        outputs->pos,
        "while evaluating the 'outputs' attribute of a derivation"
    );

    for (auto [idx, elem] : enumerate(outputs->value->listItems())) {
        // NOTE(Qyriad): This error should be *extremely* rare in practice.
        // It is impossible to construct with `stdenv.mkDerivation`,
        // `builtins.derivation`, or even `derivationStrict`. As far as we can tell,
        // it is only possible by overriding a derivation attrset already created by
        // one of those with `//` to introduce the failing `outputs` entry.
        auto errMsg = fmt("while evaluating output %d of a derivation", idx);
        std::string_view outputName = state.forceStringNoCtx(
            *elem,
            outputs->pos,
            errMsg
        );

        if (withPaths) {
            // Find the attr with this output's name...
            Attr * out = this->attrs->get(state.ctx.symbols.create(outputName));
            if (out == nullptr) {
                // FIXME: throw error?
                continue;
            }

            // Meanwhile we couldn't figure out any circumstances
            // that cause this to error.
            state.forceAttrs(*out->value, outputs->pos, errMsg);

            // ...and evaluate its `outPath` attribute.
            Attr * outPath = out->value->attrs->get(state.ctx.s.outPath);
            if (outPath == nullptr) {
                continue;
                // FIXME: throw error?
            }

            NixStringContext context;
            // And idk what could possibly cause this one to error
            // that wouldn't error before here.
            auto storePath = state.coerceToStorePath(
                outPath->pos,
                *outPath->value,
                context,
                errMsg
            );
            this->outputs.emplace(outputName, storePath);
        } else {
            this->outputs.emplace(outputName, std::nullopt);
        }
    }
}

DrvInfo::Outputs DrvInfo::queryOutputs(EvalState & state, bool withPaths, bool onlyOutputsToInstall)
{
    // If we haven't already cached the outputs set, then do so now.
    if (outputs.empty()) {
        // FIXME: this behavior seems kind of busted, since whether or not this
        // DrvInfo will have paths is forever determined by the *first* call to
        // this function??
        fillOutputs(state, withPaths);
    }

    // Things that operate on derivations like packages, like `nix-env` and `nix build`,
    // allow derivations to specify which outputs should be used in those user-facing
    // cases if the user didn't specify an output explicitly.
    // If the caller just wanted all the outputs for this derivation, though,
    // then we're done here.
    if (!onlyOutputsToInstall || !attrs)
        return outputs;

    // Regardless of `meta.outputsToInstall`, though, you can select into a derivation
    // output by its attribute, e.g. `pkgs.lix.dev`, which (lol?) sets the magic
    // attribute `outputSpecified = true`, and changes the `outputName` attr to the
    // explicitly selected-into output.
    if (Attr * outSpecAttr = attrs->get(state.ctx.s.outputSpecified)) {
        bool outputSpecified = state.forceBool(
            *outSpecAttr->value,
            outSpecAttr->pos,
            "while evaluating the 'outputSpecified' attribute of a derivation"
        );
        if (outputSpecified) {
            auto maybeOut = outputs.find(queryOutputName(state));
            if (maybeOut == outputs.end()) {
                throw Error("derivation does not have output '%s'", queryOutputName(state));
            }
            return Outputs{*maybeOut};
        }
    }

    /* Check for `meta.outputsToInstall` and return `outputs` reduced to that. */
    const Value * outTI = queryMeta(state, "outputsToInstall");
    if (!outTI) return outputs;
    auto errMsg = Error("this derivation has bad 'meta.outputsToInstall'");
        /* ^ this shows during `nix-env -i` right under the bad derivation */
    if (!outTI->isList()) throw errMsg;
    Outputs result;
    for (auto elem : outTI->listItems()) {
        if (elem->type() != nString) throw errMsg;
        auto out = outputs.find(elem->string.s);
        if (out == outputs.end()) throw errMsg;
        result.insert(*out);
    }
    return result;
}


std::string DrvInfo::queryOutputName(EvalState & state)
{
    if (outputName == "" && attrs) {
        Bindings::iterator i = attrs->find(state.ctx.s.outputName);
        outputName = i != attrs->end() ? state.forceStringNoCtx(*i->value, noPos, "while evaluating the output name of a derivation") : "";
    }
    return outputName;
}


Bindings * DrvInfo::getMeta(EvalState & state)
{
    if (meta) return meta;
    if (!attrs) return 0;
    Bindings::iterator a = attrs->find(state.ctx.s.meta);
    if (a == attrs->end()) return 0;
    state.forceAttrs(*a->value, a->pos, "while evaluating the 'meta' attribute of a derivation");
    meta = a->value->attrs;
    return meta;
}


StringSet DrvInfo::queryMetaNames(EvalState & state)
{
    StringSet res;
    if (!getMeta(state)) return res;
    for (auto & i : *meta)
        res.emplace(state.ctx.symbols[i.name]);
    return res;
}


bool DrvInfo::checkMeta(EvalState & state, Value & v)
{
    state.forceValue(v, v.determinePos(noPos));
    if (v.type() == nList) {
        for (auto elem : v.listItems())
            if (!checkMeta(state, *elem)) return false;
        return true;
    }
    else if (v.type() == nAttrs) {
        Bindings::iterator i = v.attrs->find(state.ctx.s.outPath);
        if (i != v.attrs->end()) return false;
        for (auto & i : *v.attrs)
            if (!checkMeta(state, *i.value)) return false;
        return true;
    }
    else return v.type() == nInt || v.type() == nBool || v.type() == nString ||
                v.type() == nFloat;
}


Value * DrvInfo::queryMeta(EvalState & state, const std::string & name)
{
    if (!getMeta(state)) return 0;
    Bindings::iterator a = meta->find(state.ctx.symbols.create(name));
    if (a == meta->end() || !checkMeta(state, *a->value)) return 0;
    return a->value;
}


std::string DrvInfo::queryMetaString(EvalState & state, const std::string & name)
{
    Value * v = queryMeta(state, name);
    if (!v || v->type() != nString) return "";
    return v->string.s;
}


NixInt DrvInfo::queryMetaInt(EvalState & state, const std::string & name, NixInt def)
{
    Value * v = queryMeta(state, name);
    if (!v) return def;
    if (v->type() == nInt) return v->integer;
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           integer meta fields. */
        if (auto n = string2Int<NixInt::Inner>(v->string.s))
            return NixInt{*n};
    }
    return def;
}

bool DrvInfo::queryMetaBool(EvalState & state, const std::string & name, bool def)
{
    Value * v = queryMeta(state, name);
    if (!v) return def;
    if (v->type() == nBool) return v->boolean;
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           Boolean meta fields. */
        if (strcmp(v->string.s, "true") == 0) return true;
        if (strcmp(v->string.s, "false") == 0) return false;
    }
    return def;
}


void DrvInfo::setMeta(EvalState & state, const std::string & name, Value * v)
{
    getMeta(state);
    auto attrs = state.ctx.buildBindings(1 + (meta ? meta->size() : 0));
    auto sym = state.ctx.symbols.create(name);
    if (meta)
        for (auto i : *meta)
            if (i.name != sym)
                attrs.insert(i);
    if (v) attrs.insert(sym, v);
    meta = attrs.finish();
}


/* Cache for already considered attrsets. */
typedef std::set<Bindings *> Done;


/* The result boolean indicates whether it makes sense
   for the caller to recursively search for derivations in `v'. */
static bool getDerivation(EvalState & state, Value & v,
    const std::string & attrPath, DrvInfos & drvs,
    bool ignoreAssertionFailures)
{
    try {
        state.forceValue(v, v.determinePos(noPos));
        if (!state.isDerivation(v)) return true;

        DrvInfo drv(attrPath, v.attrs);

        drv.queryName(state);

        drvs.push_back(drv);

        return false;

    } catch (AssertionError & e) {
        if (ignoreAssertionFailures) return false;
        throw;
    }
}


std::optional<DrvInfo> getDerivation(EvalState & state, Value & v,
    bool ignoreAssertionFailures)
{
    DrvInfos drvs;
    getDerivation(state, v, "", drvs, ignoreAssertionFailures);
    if (drvs.size() != 1) return {};
    return std::move(drvs.front());
}


static std::string addToPath(const std::string & s1, const std::string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static std::regex attrRegex("[A-Za-z_][A-Za-z0-9-_+]*");


/* Evaluate value `v'.  If it evaluates to a set of type `derivation',
   then put information about it in `drvs'. If it evaluates to a different
   kind of set recurse (unless it's already in `done'). */
static void getDerivations(EvalState & state, Value & vIn,
    const std::string & pathPrefix, Bindings & autoArgs,
    DrvInfos & drvs, Done & done,
    bool ignoreAssertionFailures)
{
    Value v;
    state.autoCallFunction(autoArgs, vIn, v);

    bool shouldRecurse = getDerivation(state, v, pathPrefix, drvs, ignoreAssertionFailures);
    if (!shouldRecurse) {
        // We're done here.
        return;
    }

    if (v.type() == nList) {
        // NOTE we can't really deduplicate here because small lists don't have stable addresses
        // and can cause spurious duplicate detections due to v being on the stack.
        for (auto [n, elem] : enumerate(v.listItems())) {
            std::string joinedAttrPath = addToPath(pathPrefix, fmt("%d", n));
            bool shouldRecurse = getDerivation(state, *elem, joinedAttrPath, drvs, ignoreAssertionFailures);
            if (shouldRecurse) {
                getDerivations(
                    state,
                    *elem,
                    joinedAttrPath,
                    autoArgs,
                    drvs,
                    done,
                    ignoreAssertionFailures
                );
            }
        }

        return;
    } else if (v.type() != nAttrs) {
        state.ctx.errors.make<TypeError>(
            "expression was expected to be a derivation or collection of derivations, but instead was %s",
            showType(v.type(), true)
        ).debugThrow();
    }

    /* Dont consider sets we've already seen, e.g. y in
       `rec { x.d = derivation {...}; y = x; }`. */
    auto const &[_, didInsert] = done.insert(v.attrs);
    if (!didInsert) {
        return;
    }

    // FIXME: what the fuck???
    /* !!! undocumented hackery to support combining channels in
       nix-env.cc. */
    bool combineChannels = v.attrs->find(state.ctx.symbols.create("_combineChannels")) != v.attrs->end();

    /* Consider the attributes in sorted order to get more
       deterministic behaviour in nix-env operations (e.g. when
       there are names clashes between derivations, the derivation
       bound to the attribute with the "lower" name should take
       precedence). */
    for (auto & attr : v.attrs->lexicographicOrder(state.ctx.symbols)) {
        debug("evaluating attribute '%1%'", state.ctx.symbols[attr->name]);
        // FIXME: only consider attrs with identifier-like names?? Why???
        if (!std::regex_match(std::string(state.ctx.symbols[attr->name]), attrRegex)) {
            continue;
        }
        std::string joinedAttrPath = addToPath(pathPrefix, state.ctx.symbols[attr->name]);
        if (combineChannels) {
            getDerivations(state, *attr->value, joinedAttrPath, autoArgs, drvs, done, ignoreAssertionFailures);
        } else if (getDerivation(state, *attr->value, joinedAttrPath, drvs, ignoreAssertionFailures)) {
            /* If the value of this attribute is itself a set,
               should we recurse into it?  => Only if it has a
               `recurseForDerivations = true' attribute. */
            if (attr->value->type() == nAttrs) {
                Attr * recurseForDrvs = attr->value->attrs->get(state.ctx.s.recurseForDerivations);
                if (recurseForDrvs == nullptr) {
                    continue;
                }
                bool shouldRecurse = state.forceBool(
                    *recurseForDrvs->value,
                    attr->pos,
                    fmt("while evaluating the '%s' attribute", Magenta("recurseForDerivations"))
                );
                if (!shouldRecurse) {
                    continue;
                }

                getDerivations(
                    state,
                    *attr->value,
                    joinedAttrPath,
                    autoArgs,
                    drvs,
                    done,
                    ignoreAssertionFailures
                );
            }
        }
    }
}


void getDerivations(EvalState & state, Value & v, const std::string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs, bool ignoreAssertionFailures)
{
    Done done;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, done, ignoreAssertionFailures);
}


}
