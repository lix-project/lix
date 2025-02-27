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
        []() -> Entries { co_return; },
    };
}

Fragment header = metaString("nix-archive-1");
Fragment lparen = metaString("(");
Fragment rparen = metaString(")");
Fragment type = metaString("type");

Fragment make_file(bool executable, std::string contents)
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
         [executable, contents] {
             return [](auto executable, auto contents) -> Entries {
                 co_yield File{
                     executable,
                     contents.size(),
                     [](auto contents) -> Generator<Bytes> {
                         co_yield Bytes{contents.data(), contents.size()};
                     }(contents)
                 };
             }(executable, contents);
         }},
        rparen,
    });
}

Fragment make_symlink(std::string linkTarget)
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
            [linkTarget] {
                return [](auto link) -> Entries { co_yield link; }(Symlink{linkTarget});
            },
        },
        rparen,
    });
}

Fragment make_directory(std::vector<std::pair<std::string, Fragment>> entries)
{
    std::string raw;
    std::vector<std::pair<std::string, std::function<Entries()>>> inodes;

    auto append = [&](std::string_view name, Fragment f) {
        raw += f.first;
        inodes.emplace_back(name, f.second);
    };

    for (auto & [dentryName, dentry] : entries) {
        assert(dentryName.size() <= 255);
        append("", metaString("entry"));
        append("", lparen);
        append("", metaString("name"));
        append("", metaString(dentryName));
        append("", metaString("node"));
        append(dentryName, dentry);
        append("", rparen);
    }

    return concat({
        lparen,
        type,
        metaString("directory"),
        {
            raw + rparen.first,
            [inodes] {
                return ([](auto dir) -> Entries { co_yield std::move(dir); })(Directory{
                    [](auto inodes) -> Generator<std::pair<const std::string &, Entry>> {
                        for (auto & [name, dentry] : inodes) {
                            auto subs = dentry();
                            while (auto si = subs.next()) {
                                co_yield std::pair(std::cref(name), std::move(*si));
                            }
                        }
                    }(inodes),
                });
            },
        },
    });
}

void assert_eq(Entry & a, Entry & b);

void assert_eq(File & a, File & b)
{
    ASSERT_EQ(a.executable, b.executable);
    ASSERT_EQ(a.size, b.size);
    auto acontents = GeneratorSource(std::move(a.contents)).drain();
    auto bcontents = GeneratorSource(std::move(b.contents)).drain();
    ASSERT_EQ(acontents, bcontents);
}
void assert_eq(const Symlink & a, const Symlink & b)
{
    ASSERT_EQ(a.target, b.target);
}
void assert_eq(Directory & a, Directory & b)
{
    while (true) {
        auto ae = a.contents.next();
        auto be = b.contents.next();
        ASSERT_EQ(ae.has_value(), be.has_value());
        if (!ae.has_value()) {
            break;
        }
        ASSERT_EQ(ae->first, be->first);
        assert_eq(ae->second, be->second);
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
        concat({header, make_file(false, "")}),
        concat({header, make_file(false, "short")}),
        concat({header, make_file(false, "block000")}),
        concat({header, make_file(false, "block0001")}),
        concat({header, make_file(true, "")}),
        concat({header, make_file(true, "short")}),
        concat({header, make_file(true, "block000")}),
        concat({header, make_file(true, "block0001")}),
        concat({header, make_symlink("")}),
        concat({header, make_symlink("short")}),
        concat({header, make_symlink("block000")}),
        concat({header, make_symlink("block0001")}),

        concat({header, make_directory({{"a", make_file(false, "")}})}),
        concat({header, make_directory({{"a", make_file(false, "short")}})}),
        concat({header, make_directory({{"a", make_file(false, "block000")}})}),
        concat({header, make_directory({{"a", make_file(false, "block0001")}})}),
        concat({header, make_directory({{"a", make_file(true, "")}})}),
        concat({header, make_directory({{"a", make_file(true, "short")}})}),
        concat({header, make_directory({{"a", make_file(true, "block000")}})}),
        concat({header, make_directory({{"a", make_file(true, "block0001")}})}),
        concat({header, make_directory({{"a", make_symlink("")}})}),
        concat({header, make_directory({{"a", make_symlink("short")}})}),
        concat({header, make_directory({{"a", make_symlink("block000")}})}),
        concat({header, make_directory({{"a", make_symlink("block0001")}})}),

        concat({header, make_directory({{"d", make_directory({{"a", make_file(false, "")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(false, "short")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(false, "block000")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(false, "block0001")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(true, "")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(true, "short")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(true, "block000")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_file(true, "block0001")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_symlink("")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_symlink("short")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_symlink("block000")}})}})}),
        concat({header, make_directory({{"d", make_directory({{"a", make_symlink("block0001")}})}})})
    )
);
}
