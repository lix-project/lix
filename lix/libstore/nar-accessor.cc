#include "lix/libstore/nar-accessor.hh"
#include "lix/libutil/archive.hh"

#include <map>
#include <memory>
#include <stack>
#include <algorithm>

#include <nlohmann/json.hpp>
#include <variant>

namespace nix {

struct NarAccessor : public FSAccessor
{
    std::optional<const std::string> nar;
    nar_index::Entry root;
    GetNarBytes getNarBytes;

    NarAccessor(std::string && _nar) : nar(_nar)
    {
        StringSource source(*nar);
        root = nar_index::create(source);
    }

    NarAccessor(Source & source) : root(nar_index::create(source)) {}

    NarAccessor(const std::string & listing, GetNarBytes getNarBytes)
        : getNarBytes(getNarBytes)
    {
        using namespace nar_index;
        using json = nlohmann::json;

        std::function<void(Entry &, json &)> recurse;

        recurse = [&](Entry & member, json & v) {
            std::string type = v["type"];

            if (type == "directory") {
                Directory dir;
                for (auto i = v["entries"].begin(); i != v["entries"].end(); ++i) {
                    std::string name = i.key();
                    recurse(dir.contents[name], i.value());
                }
                member = std::move(dir);
            } else if (type == "regular") {
                member = File{v.value("executable", false), v["narOffset"], v["size"]};
            } else if (type == "symlink") {
                member = Symlink{v.value("target", "")};
            } else return;
        };

        json v = json::parse(listing);
        recurse(root, v);
    }

    nar_index::Entry * find(const Path & path)
    {
        nar_index::Entry * current = &root;
        auto end = path.end();
        for (auto it = path.begin(); it != end; ) {
            // because it != end, the remaining component is non-empty so we need
            // a directory
            auto dir = std::get_if<nar_index::Directory>(current);
            if (!dir) return nullptr;

            // skip slash (canonPath above ensures that this is always a slash)
            assert(*it == '/');
            it += 1;

            // lookup current component
            auto next = std::find(it, end, '/');
            auto child = dir->contents.find(std::string(it, next));
            if (child == dir->contents.end()) return nullptr;
            current = &child->second;

            it = next;
        }

        return current;
    }

    nar_index::Entry & get(const Path & path) {
        auto result = find(path);
        if (result == nullptr)
            throw Error("NAR file does not contain path '%1%'", path);
        return *result;
    }

    Stat stat(const Path & path) override
    {
        auto i = find(path);
        if (i == nullptr)
            return {FSAccessor::Type::tMissing, 0, false};
        auto handlers = overloaded{
            [](const nar_index::File & f) {
                return Stat{tRegular, f.size, f.executable, f.offset};
            },
            [](const nar_index::Symlink &) { return Stat{tSymlink}; },
            [](const nar_index::Directory &) { return Stat{tDirectory}; },
        };
        return std::visit(handlers, *i);
    }

    StringSet readDirectory(const Path & path) override
    {
        auto & i = get(path);
        auto dir = std::get_if<nar_index::Directory>(&i);

        if (!dir)
            throw Error("path '%1%' inside NAR file is not a directory", path);

        StringSet res;
        for (auto & child : dir->contents)
            res.insert(child.first);

        return res;
    }

    std::string readFile(const Path & path, bool requireValidPath = true) override
    {
        auto & i = get(path);
        auto file = std::get_if<nar_index::File>(&i);
        if (!file)
            throw Error("path '%1%' inside NAR file is not a regular file", path);

        if (getNarBytes) return getNarBytes(file->offset, file->size);

        assert(nar);
        return std::string(*nar, file->offset, file->size);
    }

    std::string readLink(const Path & path) override
    {
        auto & i = get(path);
        auto link = std::get_if<nar_index::Symlink>(&i);
        if (!link)
            throw Error("path '%1%' inside NAR file is not a symlink", path);
        return link->target;
    }
};

ref<FSAccessor> makeNarAccessor(std::string && nar)
{
    return make_ref<NarAccessor>(std::move(nar));
}

ref<FSAccessor> makeNarAccessor(Source & source)
{
    return make_ref<NarAccessor>(source);
}

ref<FSAccessor> makeLazyNarAccessor(const std::string & listing,
    GetNarBytes getNarBytes)
{
    return make_ref<NarAccessor>(listing, getNarBytes);
}

using nlohmann::json;
json listNar(ref<FSAccessor> accessor, const Path & path, bool recurse)
{
    auto st = accessor->stat(path);

    json obj = json::object();

    switch (st.type) {
    case FSAccessor::Type::tRegular:
        obj["type"] = "regular";
        obj["size"] = st.fileSize;
        if (st.isExecutable)
            obj["executable"] = true;
        if (st.narOffset)
            obj["narOffset"] = st.narOffset;
        break;
    case FSAccessor::Type::tDirectory:
        obj["type"] = "directory";
        {
            obj["entries"] = json::object();
            json &res2 = obj["entries"];
            for (auto & name : accessor->readDirectory(path)) {
                if (recurse) {
                    res2[name] = listNar(accessor, path + "/" + name, true);
                } else
                    res2[name] = json::object();
            }
        }
        break;
    case FSAccessor::Type::tSymlink:
        obj["type"] = "symlink";
        obj["target"] = accessor->readLink(path);
        break;
    case FSAccessor::Type::tMissing:
    default:
        throw Error("path '%s' does not exist in NAR", path);
    }
    return obj;
}

}
