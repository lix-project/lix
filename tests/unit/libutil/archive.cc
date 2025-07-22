#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/serialise.hh"
#include <algorithm>
#include <gtest/gtest.h>
#include <kj/async.h>
#include <limits>

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

class NarTest : public testing::TestWithParam<std::tuple<size_t, Fragment>>
{
public:
    size_t blockSize() { return std::get<size_t>(GetParam()); }
    std::string_view raw() { return std::get<Fragment>(GetParam()).first; }
    std::function<Entries()> entriesFn() { return std::get<Fragment>(GetParam()).second; }

    Generator<Bytes> rawStream()
    {
        auto left = raw();
        while (left.size() > 0) {
            auto nextLen = std::min(left.size(), blockSize());
            co_yield std::span{left.substr(0, nextLen)};
            left.remove_prefix(nextLen);
        }
    }

    static Entries fromIndex(std::string_view raw, nar_index::Entry e)
    {
        auto handlers = overloaded{
            [&](const nar_index::File & f) -> Entry {
                std::span<const char> block{raw.substr(f.offset, f.size)};
                return File{
                    f.executable,
                    f.size,
                    [](auto block) -> Generator<Bytes> { co_yield block; }(block),
                };
            },
            [&](const nar_index::Symlink & s) -> Entry { return Symlink{s.target}; },
            [&](const nar_index::Directory & d) -> Entry {
                return Directory{
                    [](std::string_view raw, const nar_index::Directory & d
                    ) -> Generator<std::pair<const std::string &, Entry>> {
                        for (auto & [name, entry] : d.contents) {
                            co_yield std::pair{std::cref(name), *fromIndex(raw, entry).next()};
                        }
                    }(raw, d)
                };
            },
        };
        co_yield std::visit(handlers, e);
    }
};

TEST_P(NarTest, parse)
{
    GeneratorSource source(rawStream());

    auto entries = entriesFn()();
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

namespace parseAsync {
    struct File
    {
        bool executable;
        uint64_t size;
        bool closed;
        std::string contents;
        operator nar::Entry() const
        {
            auto stream = [](auto contents) -> Generator<Bytes> { co_yield contents; };
            return nar::File{executable, size, stream(std::span{contents})};
        }
    };
    struct Directory;
    using Entry = std::variant<File, Symlink, Directory>;

    struct Directory : std::map<std::string, Entry>
    {
        operator nar::Entry() const
        {
            return nar::Directory{
                [](const Directory & d) -> Generator<std::pair<const std::string &, nar::Entry>> {
                    for (auto & [name, entry] : d) {
                        co_yield std::pair{std::cref(name), *toNar(entry).next()};
                    }
                }(*this),
            };
        }

        static Entries toNar(const Entry & e)
        {
            co_yield std::visit([](const auto & e) -> Entries { co_yield e; }, e);
        }
    };

    struct ReconstructVisitor : NARParseVisitor
    {
        std::map<std::string, Entry> & parent;

        struct FileReader : NARParseVisitor::FileHandle
        {
            File & file;

            FileReader(File & file) : file(file) {}

            void receiveContents(std::string_view data) override
            {
                file.contents += data;
            }

            void close() override
            {
                file.closed = true;
            }
        };

        explicit ReconstructVisitor(std::map<std::string, Entry> & parent) : parent(parent) {}

        box_ptr<NARParseVisitor> createDirectory(const std::string & name) override
        {
            auto & dir = std::get<Directory>(parent.emplace(name, Directory{}).first->second);
            return make_box_ptr<ReconstructVisitor>(dir);
        }

        box_ptr<FileHandle>
        createRegularFile(const std::string & name, uint64_t size, bool executable) override
        {
            auto & file =
                std::get<File>(parent.emplace(name, File{executable, size, false}).first->second);
            return make_box_ptr<FileReader>(file);
        }

        void createSymlink(const std::string & name, const std::string & target) override
        {
            parent.emplace(name, Symlink{target});
        }
    };
}

TEST_P(NarTest, parseAsync)
{
    using namespace parseAsync;

    AsyncGeneratorInputStream source(rawStream());

    std::map<std::string, parseAsync::Entry> contents;
    ReconstructVisitor rv{contents};

    kj::EventLoop el;
    kj::WaitScope ws{el};

    auto entries = entriesFn()();
    parseDump(rv, source).wait(ws).value();

    for (auto & [i, val] : contents) {
        if (auto f = std::get_if<parseAsync::File>(&val)) {
            ASSERT_TRUE(f->closed);
        }
    }

    auto parsed = parseAsync::Directory::toNar(contents.at(""));
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

TEST_P(NarTest, copy)
{
    GeneratorSource source(rawStream());

    auto copied = GeneratorSource(copyNAR(source)).drain();
    ASSERT_EQ(raw(), copied);
}

TEST_P(NarTest, copyAsync)
{
    AsyncGeneratorInputStream source(rawStream());

    kj::EventLoop el;
    kj::WaitScope ws{el};
    auto copied = copyNAR(source)->drain().wait(ws).value();
    ASSERT_EQ(raw(), copied);
}

TEST_P(NarTest, parseCopied)
{
    GeneratorSource input(rawStream());
    GeneratorSource source(copyNAR(input));

    auto entries = entriesFn()();
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

    char buf;
    ASSERT_THROW(input(&buf, 1), EndOfFile);
}

TEST_P(NarTest, parseCopiedAsync)
{
    using namespace parseAsync;

    AsyncGeneratorInputStream input(rawStream());
    auto source = copyNAR(input);

    std::map<std::string, parseAsync::Entry> contents;
    ReconstructVisitor rv{contents};

    kj::EventLoop el;
    kj::WaitScope ws{el};

    auto entries = entriesFn()();
    parseDump(rv, *source).wait(ws).value();
    auto parsed = parseAsync::Directory::toNar(contents.at(""));
    while (true) {
        auto e = entries.next();
        auto p = parsed.next();
        ASSERT_EQ(e.has_value(), p.has_value());
        if (!e) {
            break;
        }
        assert_eq(*e, *p);
    }

    char buf;
    ASSERT_EQ(input.read(&buf, 1).wait(ws).value(), std::nullopt);
}

TEST_P(NarTest, index)
{
    GeneratorSource source(rawStream());

    auto entries = entriesFn()();
    auto indexed = fromIndex(raw(), nar_index::create(source));
    while (true) {
        auto e = entries.next();
        auto p = indexed.next();
        ASSERT_EQ(e.has_value(), p.has_value());
        if (!e) {
            break;
        }
        assert_eq(*e, *p);
    }
}

TEST_P(NarTest, indexAsync)
{
    AsyncGeneratorInputStream source(rawStream());

    kj::EventLoop el;
    kj::WaitScope ws{el};
    auto entries = entriesFn()();
    auto indexed = fromIndex(raw(), nar_index::create(source).wait(ws).value());
    while (true) {
        auto e = entries.next();
        auto p = indexed.next();
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
    testing::Combine(
        // test all archives with a range of block sizes, from representing
        // io that is as slow as possible, over sizes that require multiple
        // retries to fill a metadata block, then only a single retry, then
        // block sizes that fill multiple metadata blocks at once, and last
        // the block sizes that cover the entire test range. two cases will
        // be tried for this; our default block size for io (64kiB) and one
        // that is much larger (to check for strange read buffer behavior).
        testing::Values(1, 3, 7, 17, 65536, std::numeric_limits<size_t>::max()),
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
            concat({header, make_directory({{"d", make_directory({{"a", make_symlink("block0001")}})}})}),

            // Validate that files with conflicting case get correctly
            // processed through copyNAR without e.g. getting casehacked.
            //
            // By construction this can never happen anymore (since this file
            // doesn't have the case hack machinery in its closure at all!),
            // but it deserves checking anyway.
            concat({header, make_directory({{"DE", make_file(false, "meow")}, {"de", make_file(false, "mrrp")}})})
        ))
);
}
