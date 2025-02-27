#include "lix/libutil/archive.hh"
#include "lix/libutil/serialise.hh"
#include <algorithm>
#include <gtest/gtest.h>

using namespace std::literals;

namespace nix {

using namespace nar;

namespace {

using Entries = Generator<Entry>;
using Fragment = std::pair<std::string, std::function<Entries()>>;

Fragment concat(std::vector<Fragment> fragments)
{
    std::string full;
    for (auto & [part, _] : fragments) {
        full.append(part);
    }
    return {
        std::move(full),
        [fragments] {
            return [](auto fragments) -> Entries {
                for (auto & [_, part] : fragments) {
                    co_yield part();
                }
            }(fragments);
        },
    };
}

Fragment metaString(std::string s)
{
    assert(s.size() <= 255);
    return {
        char(s.size()) + "\0\0\0\0\0\0\0"s + s
            + "\0\0\0\0\0\0\0\0"s.substr(0, (8 - s.size() % 8) % 8),
        [s] { return [](auto ms) -> Entries { co_yield ms; }(MetadataString{s}); },
    };
}

auto metaRaw(std::string_view s)
{
    return [s{std::string(s)}] {
        return [](auto mr) -> Entries { co_yield mr; }(MetadataRaw{Bytes{s.data(), s.size()}});
    };
}

Fragment header = metaString("nix-archive-1");
Fragment lparen = metaString("(");
Fragment rparen = metaString(")");
Fragment type = metaString("type");

Fragment make_file(bool executable, Path path, std::string contents)
{
    assert(contents.size() <= 255);
    return concat({
        lparen,
        type,
        metaString("regular"),
        executable ? concat({metaString("executable"), metaString("")})
                   : Fragment{"", [] -> Entries { co_return; }},
        metaString("contents"),
        {char(contents.size()) + "\0\0\0\0\0\0\0"s + contents
             + "\0\0\0\0\0\0\0\0"s.substr(0, (8 - contents.size() % 8) % 8),
         [executable, path, contents] {
             return [](auto executable, auto path, auto contents) -> Entries {
                 co_yield metaRaw(char(contents.size()) + "\0\0\0\0\0\0\0"s)();
                 co_yield File{
                     path,
                     executable,
                     contents.size(),
                     [](auto contents) -> Generator<Bytes> {
                         co_yield Bytes{contents.data(), contents.size()};
                     }(contents)
                 };
                 co_yield metaRaw("\0\0\0\0\0\0\0\0"sv.substr(0, (8 - contents.size() % 8) % 8))();
             }(executable, path, contents);
         }},
        rparen,
    });
}

Fragment make_symlink(Path path, std::string linkTarget)
{
    assert(linkTarget.size() <= 255);
    return concat({
        lparen,
        type,
        metaString("symlink"),
        metaString("target"),
        metaString(linkTarget),
        {
            "",
            [path, linkTarget] {
                return [](auto link) -> Entries { co_yield link; }(Symlink{path, linkTarget});
            },
        },
        rparen,
    });
}

Fragment make_directory(Path path, std::vector<std::pair<std::string, Fragment>> entries)
{
    std::vector<Fragment> parts;
    for (auto & [dentryName, dentry] : entries) {
        assert(dentryName.size() <= 255);
        parts.push_back(metaString("entry"));
        parts.push_back(lparen);
        parts.push_back(metaString("name"));
        parts.emplace_back(metaString(dentryName));
        parts.push_back(metaString("node"));
        parts.push_back(dentry);
        parts.push_back(rparen);
    }

    auto inner = concat(parts);

    return concat({
        lparen,
        type,
        metaString("directory"),
        {
            inner.first + rparen.first,
            [path, inner] {
                return ([](auto dir) -> Entries { co_yield std::move(dir); })(Directory{
                    path,
                    [](auto inner) -> Entries {
                        co_yield std::move(inner);
                        // this should be a separate item, but the parser emits it
                        // from within the directory. but as long as it's there...
                        co_yield rparen.second();
                    }(inner.second()),
                });
            },
        },
    });
}

void assert_eq(Entry & a, Entry & b);

void assert_eq(const MetadataString & a, const MetadataString & b)
{
    ASSERT_EQ(a.data, b.data);
}
void assert_eq(const MetadataRaw & a, const MetadataRaw & b)
{
    ASSERT_PRED2(std::ranges::equal, a.raw, b.raw);
}
void assert_eq(File & a, File & b)
{
    ASSERT_EQ(a.path, b.path);
    ASSERT_EQ(a.executable, b.executable);
    ASSERT_EQ(a.size, b.size);
    auto acontents = GeneratorSource(std::move(a.contents)).drain();
    auto bcontents = GeneratorSource(std::move(b.contents)).drain();
    ASSERT_EQ(acontents, bcontents);
}
void assert_eq(const Symlink & a, const Symlink & b)
{
    ASSERT_EQ(a.path, b.path);
    ASSERT_EQ(a.target, b.target);
}
void assert_eq(Directory & a, Directory & b)
{
    ASSERT_EQ(a.path, b.path);
    while (true) {
        auto ae = a.contents.next();
        auto be = b.contents.next();
        ASSERT_EQ(ae.has_value(), be.has_value());
        if (!ae.has_value()) {
            break;
        }
        assert_eq(*ae, *be);
    }
}

void assert_eq(Entry & a, Entry & b)
{
    ASSERT_EQ(a.index(), b.index());
    return std ::visit(
        overloaded{
            []<typename T>(T & a, T & b) { assert_eq(a, b); },
            [](auto &, auto &) {},
        },
        a,
        b
    );
}
}

class NarTest : public testing::TestWithParam<Fragment>
{};

TEST_P(NarTest, parse)
{
    auto & [raw, entriesF] = GetParam();
    StringSource source(raw);

    auto entries = entriesF();
    auto parsed = parse(source);
    while (true) {
        auto e = entries.next();
        auto p = parsed.next();
        ASSERT_EQ(e.has_value(), p.has_value());
        if (!e) {
            break;
        }
        assert_eq(*e, *p);
    }
}

INSTANTIATE_TEST_SUITE_P(
    ,
    NarTest,
    testing::Values(
        concat({header, make_file(false, "", "")}),
        concat({header, make_file(false, "", "short")}),
        concat({header, make_file(false, "", "block000")}),
        concat({header, make_file(false, "", "block0001")}),
        concat({header, make_file(true, "", "")}),
        concat({header, make_file(true, "", "short")}),
        concat({header, make_file(true, "", "block000")}),
        concat({header, make_file(true, "", "block0001")}),
        concat({header, make_symlink("", "")}),
        concat({header, make_symlink("", "short")}),
        concat({header, make_symlink("", "block000")}),
        concat({header, make_symlink("", "block0001")}),

        concat({header, make_directory("", {{"a", make_file(false, "/a", "")}})}),
        concat({header, make_directory("", {{"a", make_file(false, "/a", "short")}})}),
        concat({header, make_directory("", {{"a", make_file(false, "/a", "block000")}})}),
        concat({header, make_directory("", {{"a", make_file(false, "/a", "block0001")}})}),
        concat({header, make_directory("", {{"a", make_file(true, "/a", "")}})}),
        concat({header, make_directory("", {{"a", make_file(true, "/a", "short")}})}),
        concat({header, make_directory("", {{"a", make_file(true, "/a", "block000")}})}),
        concat({header, make_directory("", {{"a", make_file(true, "/a", "block0001")}})}),
        concat({header, make_directory("", {{"a", make_symlink("/a", "")}})}),
        concat({header, make_directory("", {{"a", make_symlink("/a", "short")}})}),
        concat({header, make_directory("", {{"a", make_symlink("/a", "block000")}})}),
        concat({header, make_directory("", {{"a", make_symlink("/a", "block0001")}})}),

        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(false, "/d/a", "")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(false, "/d/a", "short")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(false, "/d/a", "block000")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(false, "/d/a", "block0001")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(true, "/d/a", "")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(true, "/d/a", "short")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(true, "/d/a", "block000")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_file(true, "/d/a", "block0001")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_symlink("/d/a", "")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_symlink("/d/a", "short")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_symlink("/d/a", "block000")}})}})}),
        concat({header, make_directory("", {{"d", make_directory("/d", {{"a", make_symlink("/d/a", "block0001")}})}})})
    )
);
}
