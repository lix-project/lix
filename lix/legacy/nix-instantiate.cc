#include "lix/libstore/globals.hh"
#include "lix/libexpr/print-ambiguous.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libexpr/value-to-xml.hh"
#include "lix/libexpr/value-to-json.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libcmd/legacy.hh"
#include "nix-instantiate.hh"

#include <iostream>


namespace nix {


static Path gcRoot;
static int rootNr = 0;


enum OutputKind { okPlain, okRaw, okXML, okJSON };

void processExpr(EvalState & state, const Strings & attrPaths,
    bool parseOnly, bool strict, Bindings & autoArgs,
    bool evalOnly, OutputKind output, bool location, Expr & e)
{
    if (parseOnly) {
        std::cout << e.toJSON(state.ctx.symbols).dump(2);
        std::cout << "\n";
        return;
    }

    Value vRoot;
    state.eval(e, vRoot);

    for (auto & i : attrPaths) {
        Value v(findAlongAttrPath(state, i, autoArgs, vRoot).first);
        state.forceValue(v, noPos);

        NixStringContext context;
        if (evalOnly) {
            Value vRes;
            if (autoArgs.empty())
                vRes = v;
            else
                state.autoCallFunction(autoArgs, v, vRes, noPos);
            if (output == okRaw)
                std::cout << *state.coerceToString(noPos, vRes, context, "while generating the nix-instantiate output", StringCoercionMode::Strict);
                // We intentionally don't output a newline here. The default PS1 for Bash in NixOS starts with a newline
                // and other interactive shells like Zsh are smart enough to print a missing newline before the prompt.
            else if (output == okXML)
                printValueAsXML(state, strict, location, vRes, std::cout, context, noPos);
            else if (output == okJSON) {
                printValueAsJSON(state, strict, vRes, noPos, std::cout, context);
                std::cout << std::endl;
            } else {
                if (strict) state.forceValueDeep(vRes);
                std::set<const void *> seen;
                printAmbiguous(vRes, state.ctx.symbols, std::cout, &seen, std::numeric_limits<int>::max());
                std::cout << std::endl;
            }
        } else {
            DrvInfos drvs;
            getDerivations(state, v, "", autoArgs, drvs, false);
            for (auto & i : drvs) {
                auto drvPath = i.requireDrvPath(state);
                auto drvPathS = state.ctx.store->printStorePath(drvPath);

                /* What output do we want? */
                std::string outputName = i.queryOutputName(state);
                if (outputName == "")
                    throw Error("derivation '%1%' lacks an 'outputName' attribute", drvPathS);

                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = absPath(gcRoot);
                    if (++rootNr > 1) rootName += "-" + std::to_string(rootNr);
                    auto store2 = state.ctx.store.try_cast_shared<LocalFSStore>();
                    if (store2)
                        drvPathS = state.aio.blockOn(store2->addPermRoot(drvPath, rootName));
                }
                std::cout << fmt("%s%s\n", drvPathS, (outputName != "out" ? "!" + outputName : ""));
            }
        }
    }
}


static int main_nix_instantiate(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        Strings files;
        bool readStdin = false;
        bool fromArgs = false;
        bool findFile = false;
        bool evalOnly = false;
        bool parseOnly = false;
        OutputKind outputKind = okPlain;
        bool xmlOutputSourceLocation = true;
        bool strict = false;
        Strings attrPaths;
        bool wantsReadWrite = false;

        struct MyArgs : LegacyArgs, MixEvalArgs
        {
            using LegacyArgs::LegacyArgs;
        };

        MyArgs myArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-instantiate");
            else if (*arg == "--version")
                printVersion("nix-instantiate");
            else if (*arg == "-")
                readStdin = true;
            else if (*arg == "--expr" || *arg == "-E")
                fromArgs = true;
            else if (*arg == "--eval" || *arg == "--eval-only")
                evalOnly = true;
            else if (*arg == "--read-write-mode")
                wantsReadWrite = true;
            else if (*arg == "--parse" || *arg == "--parse-only")
                parseOnly = evalOnly = true;
            else if (*arg == "--find-file")
                findFile = true;
            else if (*arg == "--attr" || *arg == "-A")
                attrPaths.push_back(getArg(*arg, arg, end));
            else if (*arg == "--add-root")
                gcRoot = getArg(*arg, arg, end);
            else if (*arg == "--indirect")
                ;
            else if (*arg == "--raw")
                outputKind = okRaw;
            else if (*arg == "--xml")
                outputKind = okXML;
            else if (*arg == "--json")
                outputKind = okJSON;
            else if (*arg == "--no-location")
                xmlOutputSourceLocation = false;
            else if (*arg == "--strict")
                strict = true;
            else if (*arg == "--dry-run")
                settings.readOnlyMode = true;
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                files.push_back(*arg);
            return true;
        });

        myArgs.parseCmdline(argv);

        if (evalOnly && !wantsReadWrite)
            settings.readOnlyMode = true;

        auto store = aio.blockOn(openStore());
        auto evalStore = myArgs.evalStoreUrl ? aio.blockOn(openStore(*myArgs.evalStoreUrl)) : store;

        auto evaluator = std::make_unique<Evaluator>(aio, myArgs.searchPath, evalStore, store);
        auto state = evaluator->begin(aio);
        evaluator->repair = myArgs.repair;

        Bindings & autoArgs = *myArgs.getAutoArgs(*evaluator);

        if (attrPaths.empty()) attrPaths = {""};

        if (findFile) {
            for (auto & i : files) {
                auto p = aio.blockOn(evaluator->paths.findFile(i)).unwrap();
                std::cout << p.canonical().abs() << std::endl;
            }
            return 0;
        }

        if (readStdin) {
            Expr & e = evaluator->parseStdin();
            processExpr(*state, attrPaths, parseOnly, strict, autoArgs,
                evalOnly, outputKind, xmlOutputSourceLocation, e);
        } else if (files.empty() && !fromArgs)
            files.push_back("./default.nix");

        for (auto & i : files) {
            Expr & e = fromArgs ? evaluator->parseExprFromString(i, CanonPath::fromCwd())
                                : evaluator->parseExprFromFile(evaluator->paths.resolveExprPath(
                                      aio.blockOn(lookupFileArg(*evaluator, i)).unwrap()
                                  ));
            processExpr(*state, attrPaths, parseOnly, strict, autoArgs,
                evalOnly, outputKind, xmlOutputSourceLocation, e);
        }

        evaluator->maybePrintStats();

        return 0;
    }
}

void registerLegacyNixInstantiate() {
    LegacyCommandRegistry::add("nix-instantiate", main_nix_instantiate);
}

}
