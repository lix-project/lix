#include "worker.hh"
#include "drv.hh"
#include "buffered-io.hh"

#include <nix/terminal.hh>
#include <nix/attr-path.hh>
#include <nix/local-fs-store.hh>
#include <nix/installable-flake.hh>

#include <sys/resource.h>
#include <nlohmann/json.hpp>

static nix::Value *releaseExprTopLevelValue(nix::EvalState &state,
                                            nix::Bindings &autoArgs,
                                            MyArgs &args) {
    nix::Value vTop;

    if (args.fromArgs) {
        nix::Expr *e = state.parseExprFromString(
            args.releaseExpr, state.rootPath(nix::CanonPath::fromCwd()));
        state.eval(e, vTop);
    } else {
        state.evalFile(lookupFileArg(state, args.releaseExpr), vTop);
    }

    auto vRoot = state.allocValue();

    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

static std::string attrPathJoin(nlohmann::json input) {
    return std::accumulate(input.begin(), input.end(), std::string(),
                           [](std::string ss, std::string s) {
                               // Escape token if containing dots
                               if (s.find(".") != std::string::npos) {
                                   s = "\"" + s + "\"";
                               }
                               return ss.empty() ? s : ss + "." + s;
                           });
}

void worker(nix::ref<nix::EvalState> state, nix::Bindings &autoArgs,
            nix::AutoCloseFD &to, nix::AutoCloseFD &from, MyArgs &args) {

    nix::Value *vRoot = [&]() {
        if (args.flake) {
            auto [flakeRef, fragment, outputSpec] =
                nix::parseFlakeRefWithFragmentAndExtendedOutputsSpec(
                    args.releaseExpr, nix::absPath("."));
            nix::InstallableFlake flake{
                {}, state, std::move(flakeRef), fragment, outputSpec,
                {}, {},    args.lockFlags};

            return flake.toValue(*state).first;
        } else {
            return releaseExprTopLevelValue(*state, autoArgs, args);
        }
    }();

    LineReader fromReader(from.release());

    while (true) {
        /* Wait for the collector to send us a job name. */
        if (tryWriteLine(to.get(), "next") < 0) {
            return; // main process died
        }

        auto s = fromReader.readLine();
        if (s == "exit") {
            break;
        }
        if (!nix::hasPrefix(s, "do ")) {
            fprintf(stderr, "worker error: received invalid command '%s'\n",
                    s.data());
            abort();
        }
        auto path = nlohmann::json::parse(s.substr(3));
        auto attrPathS = attrPathJoin(path);

        /* Evaluate it and send info back to the collector. */
        nlohmann::json reply =
            nlohmann::json{{"attr", attrPathS}, {"attrPath", path}};
        try {
            auto vTmp =
                nix::findAlongAttrPath(*state, attrPathS, autoArgs, *vRoot)
                    .first;

            auto v = state->allocValue();
            state->autoCallFunction(autoArgs, *vTmp, *v);

            if (v->type() == nix::nAttrs) {
                if (auto drvInfo = nix::getDerivation(*state, *v, false)) {
                    auto drv = Drv(attrPathS, *state, *drvInfo, args);
                    reply.update(drv);

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    if (args.gcRootsDir != "") {
                        nix::Path root =
                            args.gcRootsDir + "/" +
                            std::string(nix::baseNameOf(drv.drvPath));
                        if (!nix::pathExists(root)) {
                            auto localStore =
                                state->store
                                    .dynamic_pointer_cast<nix::LocalFSStore>();
                            auto storePath =
                                localStore->parseStorePath(drv.drvPath);
                            localStore->addPermRoot(storePath, root);
                        }
                    }
                } else {
                    auto attrs = nlohmann::json::array();
                    bool recurse =
                        args.forceRecurse ||
                        path.size() == 0; // Dont require `recurseForDerivations
                                          // = true;` for top-level attrset

                    for (auto &i :
                         v->attrs->lexicographicOrder(state->symbols)) {
                        const std::string &name = state->symbols[i->name];
                        attrs.push_back(name);

                        if (name == "recurseForDerivations" &&
                            !args.forceRecurse) {
                            auto attrv =
                                v->attrs->get(state->sRecurseForDerivations);
                            recurse = state->forceBool(
                                *attrv->value, attrv->pos,
                                "while evaluating recurseForDerivations");
                        }
                    }
                    if (recurse)
                        reply["attrs"] = std::move(attrs);
                    else
                        reply["attrs"] = nlohmann::json::array();
                }
            } else {
                // We ignore everything that cannot be build
                reply["attrs"] = nlohmann::json::array();
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
