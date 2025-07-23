#include "lix/libcmd/markdown.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/terminal.hh"

#include <cstdlib>
#include <iterator>
#include <new>
#include <regex>
#include <sys/queue.h>
#include <lowdown.h>

namespace nix {

static const std::string DOCROOT = "@docroot@";
static const std::string DOCROOT_URL = "https://docs.lix.systems/manual/lix/stable";

static void processLinks(struct lowdown_node * node)
{
    if (node->type == LOWDOWN_LINK) {
        struct lowdown_buf *link = &node->rndr_link.link;
        if (link && link->size && std::string_view(link->data, link->size).starts_with(DOCROOT)) {
            // link starts with @docroot@, replace that and check the path extension too.
            static std::regex mdRewrite{"\\.md(#.*)?$"}; // NOLINT(lix-foreign-exceptions)

            auto oldLink = std::string_view(link->data, link->size).substr(DOCROOT.size());
            std::string newLink = DOCROOT_URL;
            std::regex_replace(
                std::back_inserter(newLink), oldLink.begin(), oldLink.end(), mdRewrite, ".html$1"
            );
            if (link->maxsize < newLink.size()) {
                // the existing link buffer doesn't have enough space for the new string
                char *newData;
                if (!(newData = static_cast<char *>(std::realloc(link->data, newLink.size())))) {
                    throw std::bad_alloc();
                }
                link->data = newData;
                link->maxsize = newLink.size();
            }
            newLink.copy(link->data, newLink.size());
            link->size = newLink.size();
        }
    } else {
        // recurse into children
        struct lowdown_node *child;
        TAILQ_FOREACH(child, &node->children, entries)
            processLinks(child);
    }
}

std::string renderMarkdownToTerminal(std::string_view markdown, StandardOutputStream fileno)
{
    int windowWidth = getWindowSize().second;
    size_t lowdown_cols = std::max(windowWidth - 5, 60);

    struct lowdown_opts opts{
        .type = LOWDOWN_TERM,
#ifdef LOWDOWN_SEPARATE_TERM_OPTS
        .term =
            {
                .cols = lowdown_cols,
                .width = 0,
                .hmargin = 0,
                .hpadding = 4,
                .vmargin = 0,
                .centre = 0,
            },
        // maxdepth needs to be part of the ifdefs to match declaration order
        .maxdepth = 20,
#else
        .maxdepth = 20,
        .cols = lowdown_cols,
        .hmargin = 0,
        .vmargin = 0,
#endif /* LOWDOWN_SEPARATE_TERM_OPTS */
        .feat = LOWDOWN_COMMONMARK | LOWDOWN_FENCED | LOWDOWN_DEFLIST | LOWDOWN_TABLES,
        .oflags = LOWDOWN_TERM_NOLINK,
    };
    if (!shouldANSI(fileno)) {
        opts.oflags |= LOWDOWN_TERM_NOANSI;
    }

    auto doc = lowdown_doc_new(&opts);
    if (!doc)
        throw Error("cannot allocate Markdown document");
    Finally freeDoc([&]() { lowdown_doc_free(doc); });

    size_t maxn = 0;
    auto node = lowdown_doc_parse(doc, &maxn, markdown.data(), markdown.size(), nullptr);
    if (!node)
        throw Error("cannot parse Markdown document");
    Finally freeNode([&]() { lowdown_node_free(node); });

    processLinks(node);

    auto renderer = lowdown_term_new(&opts);
    if (!renderer)
        throw Error("cannot allocate Markdown renderer");
    Finally freeRenderer([&]() { lowdown_term_free(renderer); });

    auto buf = lowdown_buf_new(16384);
    if (!buf)
        throw Error("cannot allocate Markdown output buffer");
    Finally freeBuffer([&]() { lowdown_buf_free(buf); });

    int rndr_res = lowdown_term_rndr(buf, renderer, node);
    if (!rndr_res)
        throw Error("allocation error while rendering Markdown");

    return std::string(buf->data, buf->size);
}

}
