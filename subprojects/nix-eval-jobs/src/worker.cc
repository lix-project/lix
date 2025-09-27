#include <lix/config.h> // IWYU pragma: keep

// doesn't exist on macOS
// IWYU pragma: no_include <bits/types/struct_rusage.h>

#include <lix/libexpr/attr-path.hh>
#include <lix/libstore/local-fs-store.hh>
#include <lix/libcmd/installable-flake.hh>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <lix/libexpr/attr-set.hh>
#include <lix/libutil/canon-path.hh>
#include <lix/libcmd/common-eval-args.hh>
#include <lix/libutil/error.hh>
#include <lix/libexpr/eval-cache.hh>
#include <lix/libexpr/eval-inline.hh>
#include <lix/libexpr/eval.hh>
#include <lix/libexpr/flake/flakeref.hh>
#include <lix/libexpr/get-drvs.hh>
#include <lix/libutil/input-accessor.hh>
#include <lix/libutil/json.hh>
#include <lix/libutil/logging.hh>
#include <lix/libexpr/nixexpr.hh>
#include <lix/libutil/ref.hh>
#include <lix/libstore/store-api.hh>
#include <lix/libexpr/symbol-table.hh>
#include <lix/libutil/types.hh>
#include <lix/libexpr/value.hh>
#include <lix/libutil/terminal.hh>
#include <exception>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "worker.hh"
#include "drv.hh"
#include "buffered-io.hh"
#include "eval-args.hh"

static nix::Value releaseExprTopLevelValue(nix::EvalState &state,
                                           nix::Bindings &autoArgs,
                                           MyArgs &args) {
    nix::Value vTop;

    if (args.fromArgs) {
        nix::Expr &e = state.ctx.parseExprFromString(args.releaseExpr,
                                                     nix::CanonPath::fromCwd());
        state.eval(e, vTop);
    } else {
        state.evalFile(
            state.aio.blockOn(nix::lookupFileArg(state.ctx, args.releaseExpr))
                .unwrap(),
            vTop);
    }

    nix::Value vRoot;

    state.autoCallFunction(autoArgs, vTop, vRoot, {});

    return vRoot;
}

static std::string attrPathJoin(nix::JSON input) {
    return std::accumulate(input.begin(), input.end(), std::string(),
                           [](std::string ss, std::string s) {
                               // Escape token if containing dots
                               if (s.find(".") != std::string::npos) {
                                   s = "\"" + s + "\"";
                               }
                               return ss.empty() ? s : ss + "." + s;
                           });
}

static std::optional<Constituents>
readConstituents(const nix::Value *v, nix::box_ptr<nix::EvalState> &state,
                 nix::ref<nix::eval_cache::CachingEvaluator> &evaluator) {
    auto a = v->attrs()->get(state->ctx.symbols.create("_hydraAggregate"));
    if (a && state->forceBool(a->value, a->pos,
                              "while evaluating the "
                              "`_hydraAggregate` attribute")) {
        std::vector<std::string> constituents;
        std::vector<std::string> namedConstituents;
        auto a = v->attrs()->get(state->ctx.symbols.create("constituents"));
        if (!a)
            state->ctx.errors
                .make<nix::EvalError>("derivation must have a ‘constituents’ "
                                      "attribute")
                .debugThrow(nix::always_progresses); // we can't have a debugger here

        nix::NixStringContext context;
        state->coerceToString(a->pos, a->value, context,
                              "while evaluating the `constituents` attribute",
                              nix::StringCoercionMode::ToString, false);
        for (auto &c : context)
            std::visit(nix::overloaded{
                           [&](const nix::NixStringContextElem::Built &b) {
                               constituents.push_back(
                                   b.drvPath.to_string(*evaluator->store));
                           },
                           [&](const nix::NixStringContextElem::Opaque &) {},
                           [&](const nix::NixStringContextElem::DrvDeep &) {},
                       },
                       c.raw);

        state->forceList(a->value, a->pos,
                         "while evaluating the "
                         "`constituents` attribute");
        for (unsigned int n = 0; n < a->value.listSize(); ++n) {
            auto v = a->value.listElems()[n];
            state->forceValue(v, nix::noPos);
            if (v.type() == nix::nString)
                namedConstituents.emplace_back(v.str());
        }

        return Constituents(constituents, namedConstituents);
    }

    return std::nullopt;
}

void worker(nix::ref<nix::eval_cache::CachingEvaluator> evaluator,
            nix::Bindings &autoArgs, nix::AutoCloseFD &to,
            nix::AutoCloseFD &from, MyArgs &args, nix::AsyncIoRoot &aio) {

    nix::Value vRoot = [&]() {
        auto state = evaluator->begin(aio);
        if (args.flake) {
            auto [flakeRef, fragment, outputSpec] =
                nix::parseFlakeRefWithFragmentAndExtendedOutputsSpec(
                    args.releaseExpr, nix::absPath("."));
            nix::InstallableFlake flake{
                {}, evaluator, std::move(flakeRef), fragment, outputSpec,
                {}, {},        args.lockFlags};

            return flake.toValue(*state).first;
        } else {
            return releaseExprTopLevelValue(*state, autoArgs, args);
        }
    }();

    LineReader fromReader(from.release());
    auto state = evaluator->begin(aio);

    while (true) {
        /* Wait for the collector to send us a job name. */
        if (tryWriteLine(to.get(), "next") < 0) {
            return; // main process died
        }

        auto s = fromReader.readLine();
        if (s == "exit") {
            break;
        }
        if (!s.starts_with("do ")) {
            fprintf(stderr, "worker error: received invalid command '%s'\n",
                    s.data());
            abort();
        }
        auto path = nix::json::parse(s.substr(3));
        auto attrPathS = attrPathJoin(path);

        /* Evaluate it and send info back to the collector. */
        nix::JSON reply =
            nix::JSON{{"attr", attrPathS}, {"attrPath", path}};
        try {
            auto vTmp =
                nix::findAlongAttrPath(*state, attrPathS, autoArgs, vRoot)
                    .first;

            nix::Value v;
            state->autoCallFunction(autoArgs, vTmp, v, {});

            if (v.type() == nix::nAttrs) {
                if (auto drvInfo = nix::getDerivation(*state, v, false)) {
                    std::optional<Constituents> maybeConstituents;
                    if (args.constituents) {
                        maybeConstituents =
                            readConstituents(&v, state, evaluator);
                    }
                    auto drv = Drv(attrPathS, *state, *drvInfo, args,
                                   maybeConstituents);
                    reply.update(drv);

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    register_gc_root(args.gcRootsDir, drv.drvPath, evaluator->store, aio);
                } else {
                    auto attrs = nix::JSON::array();
                    bool recurse =
                        args.forceRecurse ||
                        path.size() == 0; // Dont require `recurseForDerivations
                                          // = true;` for top-level attrset

                    for (auto &i :
                         v.attrs()->lexicographicOrder(evaluator->symbols)) {
                        const std::string_view name = evaluator->symbols[i->name];
                        attrs.emplace_back(name);

                        if (name == "recurseForDerivations" &&
                            !args.forceRecurse) {
                            auto attrv = v.attrs()->get(
                                evaluator->s.recurseForDerivations);
                            recurse = state->forceBool(
                                attrv->value, attrv->pos,
                                "while evaluating recurseForDerivations");
                        }
                    }
                    if (recurse)
                        reply["attrs"] = std::move(attrs);
                    else
                        reply["attrs"] = nix::JSON::array();
                }
            } else {
                // We ignore everything that cannot be build
                reply["attrs"] = nix::JSON::array();
            }
        } catch (nix::EvalError &e) {
            auto err = e.info();
            std::ostringstream oss;
            nix::showErrorInfo(oss, err, nix::loggerSettings.showTrace.get());
            auto msg = oss.str();

            // Transmits the error we got from the previous evaluation
            // in the JSON output.
            reply["error"] = nix::filterANSIEscapes(msg, true);
            // Don't forget to print it into the STDERR log, this is
            // what's shown in the Hydra UI.
            fprintf(stderr, "%s\n", msg.c_str());
        } catch ( // NOLINT(lix-foreign-exceptions)
            const std::exception &e) { // FIXME: for some reason the catch block
                                       // above, doesn't trigger on macOS (?)
            auto msg = e.what();
            reply["error"] = nix::filterANSIEscapes(msg, true);
            fprintf(stderr, "%s\n", msg);
        }

        if (tryWriteLine(to.get(), reply.dump()) < 0) {
            return; // main process died
        }

        /* If our RSS exceeds the maximum, exit. The collector will
           start a new process. */
        struct rusage r;
        getrusage(RUSAGE_SELF, &r);
        if ((size_t)r.ru_maxrss > args.maxMemorySize * 1024)
            break;
    }

    if (tryWriteLine(to.get(), "restart") < 0) {
        return; // main process died
    };
}
