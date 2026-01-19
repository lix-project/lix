#include "dotgraph.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"

#include <iostream>
#include <sstream>

namespace nix {


static std::string dotQuote(std::string_view s)
{
    return "\"" + std::string(s) + "\"";
}


static const std::string & nextColour()
{
    static int n = 0;
    static std::vector<std::string> colours
        { "black", "red", "green", "blue"
        , "magenta", "burlywood" };
    return colours[n++ % colours.size()];
}


static std::string makeEdge(std::string_view src, std::string_view dst)
{
    return fmt("%1% -> %2% [color = %3%];\n",
        dotQuote(src), dotQuote(dst), dotQuote(nextColour()));
}


static std::string makeNode(std::string_view id, std::string_view label,
    std::string_view colour)
{
    return fmt("%1% [label = %2%, shape = box, "
        "style = filled, fillcolor = %3%];\n",
        dotQuote(id), dotQuote(label), dotQuote(colour));
}

kj::Promise<Result<std::string>> formatDotGraph(ref<Store> store, StorePathSet && roots)
try {
    StorePathSet workList(std::move(roots));
    StorePathSet doneSet;
    std::stringstream result;

    result << "digraph G {\n";

    while (!workList.empty()) {
        auto path = std::move(workList.extract(workList.begin()).value());

        if (!doneSet.insert(path).second) continue;

        result << makeNode(std::string(path.to_string()), path.name(), "#ff0000");

        for (auto & p : TRY_AWAIT(store->queryPathInfo(path))->references) {
            if (p != path) {
                workList.insert(p);
                result << makeEdge(std::string(p.to_string()), std::string(path.to_string()));
            }
        }
    }

    result << "}\n";
    co_return result.str();
} catch (...) {
    co_return result::current_exception();
}


}
