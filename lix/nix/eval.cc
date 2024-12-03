#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libexpr/print-options.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/value-to-json.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdEval : MixJSON, InstallableCommand, MixReadOnlyOption
{
    bool raw = false;
    std::optional<std::string> apply;
    std::optional<Path> writeTo;

    CmdEval() : InstallableCommand()
    {
        addFlag({
            .longName = "raw",
            .description = "Print strings without quotes or escaping.",
            .handler = {&raw, true},
        });

        addFlag({
            .longName = "apply",
            .description = "Apply the function *expr* to each argument.",
            .labels = {"expr"},
            .handler = {&apply},
        });

        addFlag({
            .longName = "write-to",
            .description = "Write a string or attrset of strings to *path*.",
            .labels = {"path"},
            .handler = {&writeTo},
        });
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    std::string doc() override
    {
        return
          #include "eval.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        if (raw && json)
            throw UsageError("--raw and --json are mutually exclusive");

        auto const installableValue = InstallableValue::require(installable);

        auto evaluator = getEvaluator();
        auto state = evaluator->begin();

        auto [v, pos] = installableValue->toValue(*state);
        NixStringContext context;

        if (apply) {
            auto vApply = evaluator->mem.allocValue();
            state->eval(evaluator->parseExprFromString(*apply, CanonPath::fromCwd()), *vApply);
            auto vRes = evaluator->mem.allocValue();
            state->callFunction(*vApply, *v, *vRes, noPos);
            v = vRes;
        }

        if (writeTo) {
            logger->pause();

            if (pathExists(*writeTo))
                throw Error("path '%s' already exists", *writeTo);

            std::function<void(Value & v, const PosIdx pos, const Path & path)> recurse;

            recurse = [&](Value & v, const PosIdx pos, const Path & path)
            {
                state->forceValue(v, pos);
                if (v.type() == nString)
                    // FIXME: disallow strings with contexts?
                    writeFile(path, v.string.s);
                else if (v.type() == nAttrs) {
                    if (mkdir(path.c_str(), 0777) == -1)
                        throw SysError("creating directory '%s'", path);
                    for (auto & attr : *v.attrs) {
                        std::string_view name = evaluator->symbols[attr.name];
                        try {
                            if (name == "." || name == "..")
                                throw Error("invalid file name '%s'", name);
                            recurse(*attr.value, attr.pos, concatStrings(path, "/", name));
                        } catch (Error & e) {
                            e.addTrace(
                                evaluator->positions[attr.pos],
                                HintFmt("while evaluating the attribute '%s'", name));
                            throw;
                        }
                    }
                }
                else
                    evaluator->errors.make<TypeError>("value at '%s' is not a string or an attribute set", evaluator->positions[pos]).debugThrow();
            };

            recurse(*v, pos, *writeTo);
        }

        else if (raw) {
            logger->pause();
            writeFull(STDOUT_FILENO, *state->coerceToString(noPos, *v, context, "while generating the eval command output"));
        }

        else if (json) {
            logger->cout("%s", printValueAsJSON(*state, true, *v, pos, context, false));
        }

        else {
            logger->cout(
                "%s",
                ValuePrinter(
                    *state,
                    *v,
                    PrintOptions {
                        .force = true,
                        .derivationPaths = true,
                        .errors = ErrorPrintBehavior::ThrowTopLevel,
                    }
                )
            );
        }
    }
};

static auto rCmdEval = registerCommand<CmdEval>("eval");
