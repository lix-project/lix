#include "lix/libstore/nar-accessor.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"

#include <map>
#include <memory>
#include <stack>
#include <algorithm>

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

        std::function<void(Entry &, const JSON &)> recurse;

        recurse = [&](Entry & member, const JSON & v) {
            std::string type = valueAt(v, "type");

            if (type == "directory") {
                auto & entries = ensureType(valueAt(v, "entries"), JSON::value_t::object);
                Directory dir;
                for (auto i = entries.begin(); i != entries.end(); ++i) {
                    std::string name = i.key();
                    recurse(dir.contents[name], i.value());
                }
                member = std::move(dir);
            } else if (type == "regular") {
                member = File{
                    ensureType(v.value("executable", false), JSON::value_t::boolean),
                    ensureType(valueAt(v, "narOffset"), JSON::value_t::number_unsigned),
                    ensureType(valueAt(v, "size"), JSON::value_t::number_unsigned),
                };
            } else if (type == "symlink") {
                member = Symlink{ensureType(v.value("target", ""), JSON::value_t::string)};
            } else {
                return;
            }
        };

        JSON v = json::parse(listing, "a nar content listing");
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

    kj::Promise<Result<Stat>> stat(const Path & path) override
    try {
        auto i = find(path);
        if (i == nullptr)
            co_return {FSAccessor::Type::tMissing, 0, false};
        auto handlers = overloaded{
            [](const nar_index::File & f) {
                return Stat{tRegular, f.size, f.executable, f.offset};
            },
            [](const nar_index::Symlink &) { return Stat{tSymlink}; },
            [](const nar_index::Directory &) { return Stat{tDirectory}; },
        };
        co_return std::visit(handlers, *i);
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<StringSet>> readDirectory(const Path & path) override
    try {
        auto & i = get(path);
        auto dir = std::get_if<nar_index::Directory>(&i);

        if (!dir)
            throw Error("path '%1%' inside NAR file is not a directory", path);

        StringSet res;
        for (auto & child : dir->contents)
            res.insert(child.first);

        co_return res;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::string>>
    readFile(const Path & path, bool requireValidPath = true) override
    try {
        auto & i = get(path);
        auto file = std::get_if<nar_index::File>(&i);
        if (!file)
            throw Error("path '%1%' inside NAR file is not a regular file", path);

        if (getNarBytes) co_return getNarBytes(file->offset, file->size);

        assert(nar);
        co_return std::string(*nar, file->offset, file->size);
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::string>> readLink(const Path & path) override
    try {
        auto & i = get(path);
        auto link = std::get_if<nar_index::Symlink>(&i);
        if (!link)
            throw Error("path '%1%' inside NAR file is not a symlink", path);
        co_return link->target;
    } catch (...) {
        co_return result::current_exception();
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

kj::Promise<Result<JSON>> listNar(ref<FSAccessor> accessor, const Path & path, bool recurse)
try {
    auto st = TRY_AWAIT(accessor->stat(path));

    JSON obj = JSON::object();

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
            obj["entries"] = JSON::object();
            JSON &res2 = obj["entries"];
            for (auto & name : TRY_AWAIT(accessor->readDirectory(path))) {
                if (recurse) {
                    res2[name] = TRY_AWAIT(listNar(accessor, path + "/" + name, true));
                } else
                    res2[name] = JSON::object();
            }
        }
        break;
    case FSAccessor::Type::tSymlink:
        obj["type"] = "symlink";
        obj["target"] = TRY_AWAIT(accessor->readLink(path));
        break;
    case FSAccessor::Type::tMissing:
    default:
        throw Error("path '%s' does not exist in NAR", path);
    }
    co_return obj;
} catch (...) {
    co_return result::current_exception();
}

static JSON listNar(const nar_index::Entry & e, Path path)
{
    JSON obj = JSON::object();

    auto handlers = overloaded{
        [&](const nar_index::File & f) {
            obj["type"] = "regular";
            obj["size"] = f.size;
            if (f.executable)
                obj["executable"] = true;
            if (f.offset)
                obj["narOffset"] = f.offset;
        },
        [&](const nar_index::Symlink & s) {
            obj["type"] = "symlink";
            obj["target"] = s.target;
        },
        [&](const nar_index::Directory & d) {
            obj["type"] = "directory";
            obj["entries"] = JSON::object();
            JSON & res2 = obj["entries"];
            for (auto & [name, entry] : d.contents) {
                res2[name] = listNar(entry, path + "/" + name);
            }
        },
    };
    std::visit(handlers, e);

    return obj;
}

JSON listNar(const nar_index::Entry & nar)
{
    return listNar(nar, "");
}

}
