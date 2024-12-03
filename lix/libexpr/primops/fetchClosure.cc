#include "lix/libexpr/primops.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/make-content-addressed.hh"
#include "lix/libutil/url.hh"

namespace nix {

/**
 * Handler for the content addressed case.
 *
 * @param state Evaluator state and store to write to.
 * @param fromStore Store containing the path to rewrite.
 * @param fromPath Source path to be rewritten.
 * @param toPathMaybe Path to write the rewritten path to. If empty, the error shows the actual path.
 * @param v Return `Value`
 */
static void runFetchClosureWithRewrite(EvalState & state, const PosIdx pos, Store & fromStore, const StorePath & fromPath, const std::optional<StorePath> & toPathMaybe, Value &v) {

    // establish toPath or throw

    if (!toPathMaybe || !state.ctx.store->isValidPath(*toPathMaybe)) {
        auto rewrittenPath = makeContentAddressed(fromStore, *state.ctx.store, fromPath);
        if (toPathMaybe && *toPathMaybe != rewrittenPath)
            throw Error({
                .msg = HintFmt("rewriting '%s' to content-addressed form yielded '%s', while '%s' was expected",
                    state.ctx.store->printStorePath(fromPath),
                    state.ctx.store->printStorePath(rewrittenPath),
                    state.ctx.store->printStorePath(*toPathMaybe)),
                .pos = state.ctx.positions[pos]
            });
        if (!toPathMaybe)
            throw Error({
                .msg = HintFmt(
                    "rewriting '%s' to content-addressed form yielded '%s'\n"
                    "Use this value for the 'toPath' attribute passed to 'fetchClosure'",
                    state.ctx.store->printStorePath(fromPath),
                    state.ctx.store->printStorePath(rewrittenPath)),
                .pos = state.ctx.positions[pos]
            });
    }

    auto toPath = *toPathMaybe;

    // check and return

    auto resultInfo = state.ctx.store->queryPathInfo(toPath);

    if (!resultInfo->isContentAddressed(*state.ctx.store)) {
        // We don't perform the rewriting when outPath already exists, as an optimisation.
        // However, we can quickly detect a mistake if the toPath is input addressed.
        throw Error({
            .msg = HintFmt(
                "The 'toPath' value '%s' is input-addressed, so it can't possibly be the result of rewriting to a content-addressed path.\n\n"
                "Set 'toPath' to an empty string to make Lix report the correct content-addressed path.",
                state.ctx.store->printStorePath(toPath)),
            .pos = state.ctx.positions[pos]
        });
    }

    state.ctx.paths.mkStorePathString(toPath, v);
}

/**
 * Fetch the closure and make sure it's content addressed.
 */
static void runFetchClosureWithContentAddressedPath(EvalState & state, const PosIdx pos, Store & fromStore, const StorePath & fromPath, Value & v) {

    if (!state.ctx.store->isValidPath(fromPath))
        copyClosure(fromStore, *state.ctx.store, RealisedPath::Set { fromPath });

    auto info = state.ctx.store->queryPathInfo(fromPath);

    if (!info->isContentAddressed(*state.ctx.store)) {
        throw Error({
            .msg = HintFmt(
                "The 'fromPath' value '%s' is input-addressed, but 'inputAddressed' is set to 'false' (default).\n\n"
                "If you do intend to fetch an input-addressed store path, add\n\n"
                "    inputAddressed = true;\n\n"
                "to the 'fetchClosure' arguments.\n\n"
                "Note that to ensure authenticity input-addressed store paths, users must configure a trusted binary cache public key on their systems. This is not needed for content-addressed paths.",
                state.ctx.store->printStorePath(fromPath)),
            .pos = state.ctx.positions[pos]
        });
    }

    state.ctx.paths.mkStorePathString(fromPath, v);
}

/**
 * Fetch the closure and make sure it's input addressed.
 */
static void runFetchClosureWithInputAddressedPath(EvalState & state, const PosIdx pos, Store & fromStore, const StorePath & fromPath, Value & v) {

    if (!state.ctx.store->isValidPath(fromPath))
        copyClosure(fromStore, *state.ctx.store, RealisedPath::Set { fromPath });

    auto info = state.ctx.store->queryPathInfo(fromPath);

    if (info->isContentAddressed(*state.ctx.store)) {
        throw Error({
            .msg = HintFmt(
                "The store object referred to by 'fromPath' at '%s' is not input-addressed, but 'inputAddressed' is set to 'true'.\n\n"
                "Remove the 'inputAddressed' attribute (it defaults to 'false') to expect 'fromPath' to be content-addressed",
                state.ctx.store->printStorePath(fromPath)),
            .pos = state.ctx.positions[pos]
        });
    }

    state.ctx.paths.mkStorePathString(fromPath, v);
}

typedef std::optional<StorePath> StorePathOrGap;

void prim_fetchClosure(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.fetchClosure");

    std::optional<std::string> fromStoreUrl;
    std::optional<StorePath> fromPath;
    std::optional<StorePathOrGap> toPath;
    std::optional<bool> inputAddressedMaybe;

    for (auto & attr : *args[0]->attrs) {
        const auto & attrName = state.ctx.symbols[attr.name];
        auto attrHint = [&]() -> std::string {
            return "while evaluating the '" + attrName + "' attribute passed to builtins.fetchClosure";
        };

        if (attrName == "fromPath") {
            NixStringContext context;
            fromPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
        }

        else if (attrName == "toPath") {
            state.forceValue(*attr.value, attr.pos);
            bool isEmptyString = attr.value->type() == nString && attr.value->string.s == std::string("");
            if (isEmptyString) {
                toPath = StorePathOrGap {};
            }
            else {
                NixStringContext context;
                toPath = state.coerceToStorePath(attr.pos, *attr.value, context, attrHint());
            }
        }

        else if (attrName == "fromStore")
            fromStoreUrl = state.forceStringNoCtx(*attr.value, attr.pos,
                    attrHint());

        else if (attrName == "inputAddressed")
            inputAddressedMaybe = state.forceBool(*attr.value, attr.pos, attrHint());

        else
            throw Error({
                .msg = HintFmt("attribute '%s' isn't supported in call to 'fetchClosure'", attrName),
                .pos = state.ctx.positions[pos]
            });
    }

    if (!fromPath)
        throw Error({
            .msg = HintFmt("attribute '%s' is missing in call to 'fetchClosure'", "fromPath"),
            .pos = state.ctx.positions[pos]
        });

    bool inputAddressed = inputAddressedMaybe.value_or(false);

    if (inputAddressed) {
        if (toPath)
            throw Error({
                .msg = HintFmt("attribute '%s' is set to true, but '%s' is also set. Please remove one of them",
                    "inputAddressed",
                    "toPath"),
                .pos = state.ctx.positions[pos]
            });
    }

    if (!fromStoreUrl)
        throw Error({
            .msg = HintFmt("attribute '%s' is missing in call to 'fetchClosure'", "fromStore"),
            .pos = state.ctx.positions[pos]
        });

    auto parsedURL = parseURL(*fromStoreUrl);

    if (parsedURL.scheme != "http" &&
        parsedURL.scheme != "https" &&
        !(getEnv("_NIX_IN_TEST").has_value() && parsedURL.scheme == "file"))
        throw Error({
            .msg = HintFmt("'fetchClosure' only supports http:// and https:// stores"),
            .pos = state.ctx.positions[pos]
        });

    if (!parsedURL.query.empty())
        throw Error({
            .msg = HintFmt("'fetchClosure' does not support URL query parameters (in '%s')", *fromStoreUrl),
            .pos = state.ctx.positions[pos]
        });

    auto fromStore = openStore(parsedURL.to_string());

    if (toPath)
        runFetchClosureWithRewrite(state, pos, *fromStore, *fromPath, *toPath, v);
    else if (inputAddressed)
        runFetchClosureWithInputAddressedPath(state, pos, *fromStore, *fromPath, v);
    else
        runFetchClosureWithContentAddressedPath(state, pos, *fromStore, *fromPath, v);
}

}
