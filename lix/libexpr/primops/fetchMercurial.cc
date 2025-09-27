#include "lix/libexpr/primops.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libutil/url-parts.hh"

namespace nix {

static void prim_fetchMercurial(EvalState & state, Value * * args, Value & v)
{
    std::string url;
    std::optional<Hash> rev;
    std::optional<std::string> ref;
    std::string_view name = "source";
    NixStringContext context;

    state.forceValue(*args[0], noPos);

    if (args[0]->type() == nAttrs) {

        for (auto & attr : *args[0]->attrs()) {
            std::string_view n(state.ctx.symbols[attr.name]);
            if (n == "url")
                url = state
                          .coerceToString(
                              attr.pos,
                              attr.value,
                              context,
                              "while evaluating the `url` attribute passed to "
                              "builtins.fetchMercurial",
                              StringCoercionMode::Strict,
                              false
                          )
                          .toOwned();
            else if (n == "rev") {
                // Ugly: unlike fetchGit, here the "rev" attribute can
                // be both a revision or a branch/tag name.
                auto value = state.forceStringNoCtx(
                    attr.value,
                    attr.pos,
                    "while evaluating the `rev` attribute passed to builtins.fetchMercurial"
                );
                if (std::regex_match(value.begin(), value.end(), revRegex)) {
                    rev = Hash::parseAny(value, HashType::SHA1);
                } else
                    ref = value;
            }
            else if (n == "name")
                name = state.forceStringNoCtx(
                    attr.value,
                    attr.pos,
                    "while evaluating the `name` attribute passed to builtins.fetchMercurial"
                );
            else {
                state.ctx.errors
                    .make<EvalError>(
                        "unsupported argument '%s' to 'fetchMercurial'",
                        state.ctx.symbols[attr.name]
                    )
                    .atPos(attr.pos)
                    .debugThrow();
            }
        }

        if (url.empty())
            state.ctx.errors.make<EvalError>("'url' argument required").debugThrow();

    } else
        url = state.coerceToString(noPos, *args[0], context,
                "while evaluating the first argument passed to builtins.fetchMercurial",
                StringCoercionMode::Strict, false).toOwned();

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.ctx.paths.checkURI(url);

    if (evalSettings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision");

    fetchers::Attrs attrs;
    attrs.insert_or_assign("type", "hg");
    attrs.insert_or_assign("url", url.find("://") != std::string::npos ? url : "file://" + url);
    attrs.insert_or_assign("name", std::string(name));
    if (ref) attrs.insert_or_assign("ref", *ref);
    if (rev) attrs.insert_or_assign("rev", rev->gitRev());
    auto input = fetchers::Input::fromAttrs(std::move(attrs));

    // FIXME: use name
    auto [tree, input2] = state.aio.blockOn(input.fetch(state.ctx.store));

    auto attrs2 = state.ctx.buildBindings(8);
    state.ctx.paths.mkStorePathString(tree.storePath, attrs2.alloc(state.ctx.s.outPath));
    if (input2.getRef())
        attrs2.alloc("branch").mkString(*input2.getRef());
    // Backward compatibility: set 'rev' to
    // 0000000000000000000000000000000000000000 for a dirty tree.
    auto rev2 = input2.getRev().value_or(Hash(HashType::SHA1));
    attrs2.alloc("rev").mkString(rev2.gitRev());
    attrs2.alloc("shortRev").mkString(rev2.gitRev().substr(0, 12));
    if (auto revCount = input2.getRevCount())
        attrs2.alloc("revCount").mkInt(*revCount);
    v.mkAttrs(attrs2);

    state.ctx.paths.allowPath(tree.storePath);
}

static RegisterPrimOp r_fetchMercurial({
    .name = "fetchMercurial",
    .arity = 1,
    .fun = prim_fetchMercurial
});

}
