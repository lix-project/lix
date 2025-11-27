#include "eval.hh"
#include "eval-inline.hh"
#include "primops.hh"
#include "gc-small-vector.hh"

/// This file contains all implementations of `Expr::eval`, and some other helper functions defined by
/// `Expr` subtypes. Note that some of the evaluation helper functions on `EvalState` that do the heavy
/// lifting are not in this file but kept in `eval.cc`. In the future, more logic from here will be factored
/// out into helpers over at `eval.cc` until this file contains a readable and high-level implementation of
/// the evaluator.

namespace nix {

/* Create a thunk for the delayed computation of the given expression
   in the given environment.  But if the expression is a variable,
   then look it up right away.  This significantly reduces the number
   of thunks allocated. */
Value Expr::maybeThunk(EvalState & state, Env & env)
{
    state.ctx.stats.nrThunks++;
    return {NewValueAs::thunk, state.ctx.mem, env, *this};
}

Value ExprVar::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.lookupVar(&env, *this, true);
    /* The value might not be initialised in the environment yet.
       In that case, ignore it. */
    if (v && !v->isInvalid()) {
        state.ctx.stats.nrAvoided++;
        return *v;
    }
    return Expr::maybeThunk(state, env);
}

Value ExprLiteral::maybeThunk(EvalState & state, Env & env)
{
    state.ctx.stats.nrAvoided++;
    return v;
}

Value ExprList::maybeThunk(EvalState & state, Env & env)
{
    if (elems.empty()) {
        return Value::EMPTY_LIST;
    }
    return Expr::maybeThunk(state, env);
}

void Expr::eval(EvalState & state, Env & env, Value & v)
{
    abort();
}

void ExprLiteral::eval(EvalState & state, Env & env, Value & v)
{
    v = this->v;
}

void ExprInheritFrom::eval(EvalState & state, Env & env, Value & v)
{
    Value & v2 = env.values[displ];
    state.forceValue(v2, pos);
    v = v2;
}

Env * ExprAttrs::buildInheritFromEnv(EvalState & state, Env & up)
{
    Env & inheritEnv = state.ctx.mem.allocEnv(inheritFromExprs->size());
    inheritEnv.up = &up;

    Displacement displ = 0;
    for (auto & from : *inheritFromExprs) {
        inheritEnv.values[displ++] = from->maybeThunk(state, up);
    }

    return &inheritEnv;
}

void ExprSet::eval(EvalState & state, Env & env, Value & v)
{
    Bindings::Size capacity = attrs.size() + dynamicAttrs.size();
    v.mkAttrs(state.ctx.buildBindings(capacity).finish());
    auto dynamicEnv = &env;

    if (recursive) {

        /* Create a new environment that contains the attributes in
           this `rec'. */
        Env & env2(state.ctx.mem.allocEnv(attrs.size()));
        env2.up = &env;
        dynamicEnv = &env2;
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env2) : nullptr;

        ExprAttrs::AttrDefs::iterator overrides = attrs.find(state.ctx.symbols.sym___overrides);
        bool hasOverrides = overrides != attrs.end();

        /* The recursive attributes are evaluated in the new
           environment, while the inherited attributes are evaluated
           in the original environment. */
        Displacement displ = 0;
        for (auto & i : attrs) {
            Value vAttr;
            if (hasOverrides && i.second.kind != ExprAttrs::AttrDef::Kind::Inherited) {
                vAttr = {
                    NewValueAs::thunk,
                    state.ctx.mem,
                    *i.second.chooseByKind(&env2, &env, inheritEnv),
                    *i.second.e
                };
                state.ctx.stats.nrThunks++;
            } else {
                vAttr = i.second.e->maybeThunk(state, *i.second.chooseByKind(&env2, &env, inheritEnv));
            }
            env2.values[displ++] = vAttr;
            v.attrs()->push_back(Attr(i.first, vAttr, i.second.pos));
        }

        /* If the rec contains an attribute called `__overrides', then
           evaluate it, and add the attributes in that set to the rec.
           This allows overriding of recursive attributes, which is
           otherwise not possible.  (You can use the // operator to
           replace an attribute, but other attributes in the rec will
           still reference the original value, because that value has
           been substituted into the bodies of the other attributes.
           Hence we need __overrides.) */
        if (hasOverrides) {
            Value & vOverrides = (*v.attrs())[overrides->second.displ].value;
            state.forceAttrs(vOverrides, noPos, "while evaluating the `__overrides` attribute");
            Bindings * newBnds = state.ctx.mem.allocBindings(capacity + vOverrides.attrs()->size());
            for (auto & i : *v.attrs()) {
                newBnds->push_back(i);
            }
            for (auto & i : *vOverrides.attrs()) {
                ExprAttrs::AttrDefs::iterator j = attrs.find(i.name);
                if (j != attrs.end()) {
                    (*newBnds)[j->second.displ] = i;
                    env2.values[j->second.displ] = i.value;
                } else {
                    newBnds->push_back(i);
                }
            }
            newBnds->sort();
            v.mkAttrs(newBnds);
        }
    }

    else {
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env) : nullptr;
        for (auto & i : attrs) {
            v.attrs()->push_back(Attr(
                i.first,
                i.second.e->maybeThunk(state, *i.second.chooseByKind(&env, &env, inheritEnv)),
                i.second.pos
            ));
        }
    }

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        /* Before evaluating dynamic attrs, we blackhole the output attrset and only restore it after the operation.
         * This is to avoid exposing the partially constructed set as a value, see
         * http://github.com/NixOS/nix/issues/7012. Any accesses to the output attrset will thus infrec.
         */
        Value vBackup = v;
        Value nameVal;
        {
            KJ_DEFER(v = vBackup);
            v = Value{NewValueAs::blackhole};
            i.nameExpr->eval(state, *dynamicEnv, nameVal);
            state.forceValue(nameVal, i.pos);
            if (nameVal.type() == nNull) {
                continue;
            }
            state.forceStringNoCtx(nameVal, i.pos, "while evaluating the name of a dynamic attribute");
        }
        auto nameSym = state.ctx.symbols.create(nameVal.str());
        auto j = v.attrs()->get(nameSym);
        if (j) {
            state.ctx.errors
                .make<EvalError>(
                    "dynamic attribute '%1%' already defined at %2%",
                    state.ctx.symbols[nameSym],
                    state.ctx.positions[j->pos]
                )
                .atPos(i.pos)
                .withFrame(env, *this)
                .debugThrow();
        }

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.attrs()->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), i.pos));
        v.attrs()->sort(); // FIXME: inefficient
    }

    v.attrs()->pos = pos;
}

void ExprLet::eval(EvalState & state, Env & env, Value & v)
{
    /* Create a new environment that contains the attributes in this
       `let'. */
    Env & env2(state.ctx.mem.allocEnv(attrs.size()));
    env2.up = &env;

    Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env2) : nullptr;

    /* The recursive attributes are evaluated in the new environment,
       while the inherited attributes are evaluated in the original
       environment. */
    Displacement displ = 0;
    for (auto & i : attrs) {
        env2.values[displ++] = i.second.e->maybeThunk(state, *i.second.chooseByKind(&env2, &env, inheritEnv));
    }

    body->eval(state, env2, v);
}

void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    auto result = state.ctx.mem.newList(elems.size());
    v = {NewValueAs::list, result};
    for (auto && [n, v2] : enumerate(result->span())) {
        v2 = elems[n]->maybeThunk(state, env);
    }
}

void ExprVar::eval(EvalState & state, Env & env, Value & v)
{
    Value * v2 = state.lookupVar(&env, *this, false);
    try {
        state.forceValue(*v2, pos);
    } catch (Error & e) {
        /* `name` can be invalid if we are an ExprInheritFrom */
        if (name) {
            e.addTrace(state.ctx.positions[getPos()], "while evaluating %s", state.ctx.symbols[name]);
        }
        throw;
    }
    v = *v2;
}

void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.ctx.mem.allocEnv(1));
    env2.up = &env;
    env2.values[0] = attrs->maybeThunk(state, env);

    body->eval(state, env2, v);
}

void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    Value vCond;
    cond->eval(state, env, vCond);
    (state.checkBool(vCond, env, *cond) ? *then : *else_).eval(state, env, v);
}

void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    Value vCond;
    cond->eval(state, env, vCond);
    if (!state.checkBool(vCond, env, *cond)) {
        state.ctx.errors.make<AssertionError>("assertion failed")
            .atPos(pos)
            .withFrame(env, *this)
            .debugThrow();
    }
    body->eval(state, env, v);
}

void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    Value vInner;
    e->eval(state, env, vInner);
    v.mkBool(!state.checkBool(vInner, env, *e));
}

void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    Value v2;
    e2->eval(state, env, v2);
    v.mkBool(state.eqValues(v1, v2, pos, "while testing two values for equality"));
}

void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    Value v2;
    e2->eval(state, env, v2);
    v.mkBool(!state.eqValues(v1, v2, pos, "while testing two values for inequality"));
}

void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    /* Explicitly short-circuit */
    if (!state.checkBool(v1, env, *e1)) {
        v.mkBool(false);
        return;
    }
    Value v2;
    e2->eval(state, env, v2);
    v.mkBool(state.checkBool(v2, env, *e2));
}

void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    /* Explicitly short-circuit */
    if (state.checkBool(v1, env, *e1)) {
        v.mkBool(true);
        return;
    }
    Value v2;
    e2->eval(state, env, v2);
    v.mkBool(state.checkBool(v2, env, *e2));
}

void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    /* Explicitly short-circuit (ex falso quodlibet) */
    if (!state.checkBool(v1, env, *e1)) {
        v.mkBool(true);
        return;
    }
    Value v2;
    e2->eval(state, env, v2);
    v.mkBool(state.checkBool(v2, env, *e2));
}

void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1;
    e1->eval(state, env, v1);
    state.checkAttrs(v1, env, *e1);
    Value v2;
    e2->eval(state, env, v2);
    state.checkAttrs(v2, env, *e2);

    state.ctx.stats.nrOpUpdates++;

    if (v1.attrs()->size() == 0) {
        v = v2;
        return;
    }
    if (v2.attrs()->size() == 0) {
        v = v1;
        return;
    }

    auto attrs = state.ctx.buildBindings(v1.attrs()->size() + v2.attrs()->size());

    /* Merge the sets, preferring values from the second set.  Make
       sure to keep the resulting vector in sorted order. */
    Bindings::iterator i = v1.attrs()->begin();
    Bindings::iterator j = v2.attrs()->begin();

    while (i != v1.attrs()->end() && j != v2.attrs()->end()) {
        if (i->name == j->name) {
            attrs.insert(*j);
            ++i;
            ++j;
        } else if (i->name < j->name) {
            attrs.insert(*i++);
        } else {
            attrs.insert(*j++);
        }
    }

    while (i != v1.attrs()->end()) {
        attrs.insert(*i++);
    }
    while (j != v2.attrs()->end()) {
        attrs.insert(*j++);
    }

    v.mkAttrs(attrs.alreadySorted());

    state.ctx.stats.nrOpUpdateValuesCopied += v.attrs()->size();
}

void ExprOpConcatLists::eval(EvalState & state, Env & env, Value & v)
{
    state.ctx.stats.nrListConcats++;

    /* We don't call into `concatLists` as that loses the position information of the expressions. */

    Value v1;
    e1->eval(state, env, v1);
    state.checkList(v1, env, *e1);
    Value v2;
    e2->eval(state, env, v2);
    state.checkList(v2, env, *e2);

    size_t l1 = v1.listSize(), l2 = v2.listSize(), len = l1 + l2;

    if (l1 == 0) {
        v = v2;
    } else if (l2 == 0) {
        v = v1;
    } else {
        auto list = state.ctx.mem.newList(len);
        v = {NewValueAs::list, list};
        auto out = list->elems;
        std::copy(v1.listElems(), v1.listElems() + l1, out);
        std::copy(v2.listElems(), v2.listElems() + l2, out + l1);
    }
}

void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    NixStringContext context;
    std::vector<BackedStringView> s;
    size_t sSize = 0;
    NixInt n{0};
    NixFloat nf = 0;

    bool first = !isInterpolation;
    ValueType firstType = nString;

    const auto str = [&] {
        std::string result;
        result.reserve(sSize);
        for (const auto & part : s) {
            result += *part;
        }
        return result;
    };
    /* build a gc'd value string directly instead of going through str()
       and mkString to save an allocation and copy */
    const auto gcStr = [&] {
        auto result = Value::Str::gcAlloc(sSize);

        char * tmp = result->contents;
        for (const auto & part : s) {
            memcpy(tmp, part->data(), part->size());
            tmp += part->size();
        }
        return result;
    };

    // List of returned strings. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> values(es.size());
    Value * vTmpP = values.data();

    for (auto & [i_pos, i] : es) {
        Value & vTmp = *vTmpP++;
        i->eval(state, env, vTmp);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            firstType = vTmp.type();
        }

        if (firstType == nInt) {
            if (vTmp.type() == nInt) {
                auto newN = n + vTmp.integer();
                if (auto checked = newN.valueChecked(); checked.has_value()) {
                    n = NixInt(*checked);
                } else {
                    state.ctx.errors
                        .make<EvalError>("integer overflow in adding %1% + %2%", n, vTmp.integer())
                        .atPos(i_pos)
                        .debugThrow();
                }
            } else if (vTmp.type() == nFloat) {
                // Upgrade the type from int to float;
                firstType = nFloat;
                nf = n.value;
                nf += vTmp.fpoint();
            } else {
                state.ctx.errors.make<EvalError>("cannot add %1% to an integer", showType(vTmp))
                    .atPos(i_pos)
                    .withFrame(env, *this)
                    .debugThrow();
            }
        } else if (firstType == nFloat) {
            if (vTmp.type() == nInt) {
                nf += vTmp.integer().value;
            } else if (vTmp.type() == nFloat) {
                nf += vTmp.fpoint();
            } else {
                state.ctx.errors.make<EvalError>("cannot add %1% to a float", showType(vTmp))
                    .atPos(i_pos)
                    .withFrame(env, *this)
                    .debugThrow();
            }
        } else {
            if (s.empty()) {
                s.reserve(es.size());
            }

            /* If we are coercing inside of an interpolation, we may allow slightly more comfort by coercing
             * things like integers. */
            auto coercionMode = isInterpolation && featureSettings.isEnabled(Xp::CoerceIntegers)
                ? StringCoercionMode::Interpolation
                : StringCoercionMode::Strict;

            /* skip canonization of first path, which would only be not
            canonized in the first place if it's coming from a ./${foo} type
            path */
            auto part = state.coerceToString(
                i_pos,
                vTmp,
                context,
                "while evaluating a path segment",
                coercionMode,
                firstType == nString,
                !first
            );
            sSize += part->size();
            s.emplace_back(std::move(part));
        }

        first = false;
    }

    if (firstType == nInt) {
        v.mkInt(n);
    } else if (firstType == nFloat) {
        v.mkFloat(nf);
    } else if (firstType == nPath) {
        if (!context.empty()) {
            state.ctx.errors
                .make<EvalError>("a string that refers to a store path cannot be appended to a path")
                .atPos(pos)
                .withFrame(env, *this)
                .debugThrow();
        }
        v.mkPath(CanonPath(canonPath(str())));
    } else {
        v.mkStringMove(gcStr(), context);
    }
}

void ExprPos::eval(EvalState & state, Env & env, Value & v)
{
    state.mkPos(v, pos);
}

void ExprBlackHole::eval(EvalState & state, Env & env, Value & v)
{
    state.ctx.errors.make<InfiniteRecursionError>("infinite recursion encountered").debugThrow();
}

void ExprDebugFrame::eval(EvalState & state, Env & env, Value & v)
{
    auto dts = makeDebugTraceStacker(state, *inner, env, state.ctx.positions[pos], message);
    inner->eval(state, env, v);
}

/** Returns `nullptr` if we should be using a default instead. */
Attr const *
ExprSelect::selectSingleAttr(EvalState & state, Env & env, AttrName const & attrName, Value & vCurrent)
{
    Symbol const attrSym = getName(attrName, state, env);

    try {
        state.forceValue(vCurrent, pos);
    } catch (Error & e) {
        // clang-format off
        e.addTrace(state.ctx.positions[attrName.pos], HintFmt(
            "while evaluating an expression to select '%s' on it", state.ctx.symbols[attrSym]
        ));
        // clang-format on
        throw;
    }

    if (vCurrent.type() != nAttrs) {
        // If we have an `or` provided default, then it doesn't have to be an attrset.
        // Let the caller know there's no attr value here.
        if (def != nullptr) {
            return nullptr;
        }

        // Otherwise, we must type error.
        // clang-format off
        state.ctx.errors.make<TypeError>(
            "expected a set but found %s: %s",
            showType(vCurrent),
            ValuePrinter(state, vCurrent, errorPrintOptions)
        ).addTrace(
            attrName.pos,
            HintFmt("while selecting '%s'", state.ctx.symbols[attrSym])
        ).debugThrow();
        // clang-format on
    }

    // Now that we know it's an attrset, we can actually look for the name.

    auto const attrIt = vCurrent.attrs()->get(attrSym);
    if (!attrIt) {

        // Again if we have an `or` provided default, then missing attr is not an error.
        if (def != nullptr) {
            return nullptr;
        }

        // Otherwise, we collect all attr names and throw an attr missing error.

        std::set<std::string> const allAttrNames = *vCurrent.attrs()
            | std::views::transform([&state](auto const & attr) {
                  return std::string{state.ctx.symbols[attr.name]};
              })
            | std::ranges::to<std::set>();

        auto suggestions = Suggestions::bestMatches(allAttrNames, state.ctx.symbols[attrSym]);
        state.ctx.errors.make<EvalError>("attribute '%s' missing", state.ctx.symbols[attrSym])
            .atPos(attrName.pos)
            .withSuggestions(suggestions)
            .withFrame(env, *this)
            .debugThrow();
    }

    // If we made it here, then we successfully found the attribute.
    // Return it to our caller!

    return attrIt;
}

void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    // Position for the current attrset Value in this select chain.
    PosIdx posCurrent;
    // Position for the current selector in this select chain.
    PosIdx posCurrentSyntax;

    Value baseSelectee;
    try {
        // Evaluate the original thing we're selecting on.
        e->eval(state, env, baseSelectee);
    } catch (Error & e) {
        // clang-format off
        e.addTrace(state.ctx.positions[getPos()], HintFmt(
            "while evaluating an expression to select '%s' on it",
            showAttrPath(state.ctx.symbols, attrPath)
        ));
        // clang-format on
        throw;
    }

    try {
        // With the original selectee evaluated, we'll walk the selection path starting
        // with the evaluated original selectee.
        std::reference_wrapper<Value> curSelectee = std::ref(baseSelectee);
        for (AttrName const & attrName : attrPath) {
            state.ctx.stats.nrLookups++;

            // Select `attrName` on `curSelectee`.
            auto const attr = selectSingleAttr(state, env, attrName, curSelectee.get());
            if (!attr) {
                // Use default.
                try {
                    this->def->eval(state, env, v);
                } catch (Error & err) {
                    err.addTrace(
                        state.ctx.positions[this->def->pos],
                        "while evaluating fallback for missing attribute '%s'",
                        state.ctx.symbols[getName(attrName, state, env)]
                    );
                    throw;
                }
                return;
            }

            // The selection worked. If we have another iteration, then we use `attr->value`
            // as the thing to select on. If this is the last iteration, then `attr->value`
            // is the final value this ExprSelect evaluated to.
            curSelectee = std::ref(attr->value);

            posCurrent = attr->pos;
            posCurrentSyntax = attrName.pos;
            if (state.ctx.stats.countCalls) {
                state.ctx.stats.attrSelects[posCurrent]++;
            }
        }

        state.forceValue(curSelectee.get(), posCurrent ? posCurrent : posCurrentSyntax);

        v = curSelectee.get();

    } catch (Error & err) {
        auto const & lastPos = state.ctx.positions[posCurrent];
        if (lastPos && !std::get_if<Pos::Hidden>(&lastPos.origin)) {
            err.addTrace(lastPos, "while evaluating the attribute '%s'", showAttrPath(state, env, attrPath));
        }

        throw;
    }
}

void ExprOpHasAttr::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    for (auto & i : attrPath) {
        state.forceValue(*vAttrs, getPos());
        const Attr * j;
        auto name = getName(i, state, env);
        if (vAttrs->type() != nAttrs || (j = vAttrs->attrs()->get(name)) == nullptr) {
            v.mkBool(false);
            return;
        } else {
            vAttrs = &j->value;
        }
    }

    v.mkBool(true);
}

void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v = {NewValueAs::lambda, state.ctx.mem, env, *this};
}

void ExprCall::eval(EvalState & state, Env & env, Value & v)
{
    Value vFun;
    fun->eval(state, env, vFun);

    // Empirical arity of Nixpkgs lambdas by regex e.g. ([a-zA-Z]+:(\s|(/\*.*\/)|(#.*\n))*){5}
    // 2: over 4000
    // 3: about 300
    // 4: about 60
    // 5: under 10
    // This excluded attrset lambdas (`{...}:`). Contributions of mixed lambdas appears insignificant at ~150
    // total.
    SmallValueVector<4> vArgs(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        vArgs[i] = args[i]->maybeThunk(state, env);
    }

    state.callFunction(vFun, vArgs, v, pos);
}

}
