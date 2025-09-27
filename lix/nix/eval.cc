#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libexpr/print-options.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/value-to-json.hh"
#include "eval.hh"
#include "lix/libutil/types.hh"

namespace nix {

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

        // `--write-to` was axed because it was not used in-tree, no non-packaging uses out of tree
        // could be found, and it was rife with misvehavior including arbitrary file writes as root
        // when run a prepared input. we have opted to remove it instead of trying to make it safe.
        addFlag({
            .longName = "write-to",
            .description = "Previously used to write a string or attrset of strings to *path*.",
            .labels = {"path"},
            .handler = {&writeTo},
            .hidden = true,
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

        if (writeTo) {
            throw UsageError(
                "--write-to has been removed because it was insecure and broken, please use "
                "structured output formats (e.g. via --json) instead"
            );
        }

        auto const installableValue = InstallableValue::require(installable);

        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());

        auto [v, pos] = installableValue->toValue(*state);
        NixStringContext context;

        if (apply) {
            Value vApply;
            state->eval(evaluator->parseExprFromString(*apply, CanonPath::fromCwd()), vApply);
            Value vRes;
            state->callFunction(vApply, v, vRes, noPos);
            v = vRes;
        }

        if (raw) {
            logger->pause();
            writeFull(
                STDOUT_FILENO,
                *state->coerceToString(
                    noPos, v, context, "while generating the eval command output"
                )
            );
        }

        else if (json)
        {
            logger->cout("%s", printValueAsJSON(*state, true, v, pos, context, false));
        }

        else
        {
            logger->cout(
                "%s",
                ValuePrinter(
                    *state,
                    v,
                    PrintOptions{
                        .force = true,
                        .derivationPaths = true,
                        .errors = ErrorPrintBehavior::ThrowTopLevel,
                    }
                )
            );
        }
    }
};

void registerNixEval()
{
    registerCommand<CmdEval>("eval");
}

}
