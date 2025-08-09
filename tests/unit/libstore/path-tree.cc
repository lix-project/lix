#include <gtest/gtest.h>
#include "lix/libstore/path-tree.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libstore/nar-accessor.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/json.hh"

namespace nix {
static ref<FSAccessor> makeMockAccessor()
{
    JSON nix = JSON::object();
    nix["type"] = "directory";

    JSON store = JSON::object();
    nix["entries"] = JSON::object();

    store["type"] = "directory";
    store["entries"] = JSON::object();

    const std::string sshContent =
        "I do link to /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66/lib/libc.so.6 in "
        "here";
    const auto narOffset = 2342ul;

    auto openssh = JSON::object();
    openssh["type"] = "directory";
    openssh["entries"] = JSON::object();
    openssh["entries"]["bin"] = JSON::object();
    openssh["entries"]["bin"]["type"] = "directory";
    openssh["entries"]["bin"]["entries"] = JSON::object();
    openssh["entries"]["bin"]["entries"]["ssh"] = JSON::object();
    openssh["entries"]["bin"]["entries"]["ssh"]["type"] = "regular";
    openssh["entries"]["bin"]["entries"]["ssh"]["size"] = sshContent.size();
    openssh["entries"]["bin"]["entries"]["ssh"]["narOffset"] = narOffset;
    store["entries"]["hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2"] = openssh;

    nix["entries"]["store"] = store;

    JSON listing = JSON::object();
    listing["type"] = "directory";
    listing["entries"] = JSON::object();
    listing["entries"]["nix"] = nix;

    return makeLazyNarAccessor(
        listing.dump(),
        [narOffset, sshContent](uint64_t offset, uint64_t length) {
            if (offset == narOffset) {
                assert(length <= sshContent.size());
                return sshContent.substr(0, length);
            } else {
                throw Error(
                    "Invalid offset '%llu' in mock NAR (looking for: %llu)", offset, narOffset
                );
            }
        }
    );
}

TEST(PathTree, simple)
{
    AsyncIoRoot aio;

    auto store = aio.blockOn(openStore("dummy://"));

    auto parent = StorePath{"hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2"};
    auto child = StorePath{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66"};

    std::map<StorePath, StorePathSet> graph;
    graph.insert({child, {}});
    graph.insert({parent, {child}});

    auto graph_data =
        aio.blockOn(genGraphString(parent, child, graph, *store, false, false, makeMockAccessor()));

    // clang-format off
    ASSERT_EQ(
        graph_data,
               "/nix/store/hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2\n"
        "\x1B[1m└───/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66\x1B[0m"
    );
    // clang-format on
}

TEST(PathTree, precise)
{
    AsyncIoRoot aio;

    auto store = aio.blockOn(openStore("dummy://"));

    auto parent = StorePath{"hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2"};
    auto child = StorePath{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66"};

    std::map<StorePath, StorePathSet> graph;
    graph.insert({child, {}});
    graph.insert({parent, {child}});

    auto graph_data =
        aio.blockOn(genGraphString(parent, child, graph, *store, false, true, makeMockAccessor()));

    // clang-format off
    ASSERT_EQ(
        graph_data,
        "/nix/store/hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2\x1B[0m\n"
        "└───bin/ssh: …I do link to /nix/store/\x1B[32;1maaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\x1B[0m-glibc-2.40-66/lib/libc.so.6 in …\n"
        "    \x1B[0m→ /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66\x1B[0m"
    );
    // clang-format on
}

TEST(PathTree, all)
{
    AsyncIoRoot aio;

    auto store = aio.blockOn(openStore("dummy://"));

    auto parent = StorePath{"hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2"};
    auto child = StorePath{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66"};
    auto intermediate = StorePath{"6r4zqb04fq5l5l4zghq76wvcpz7dwd35-linux-pam-1.6.1"};

    std::map<StorePath, StorePathSet> graph;
    graph.insert({intermediate, {child}});
    graph.insert({parent, {intermediate, child}});
    graph.insert({child, {}});

    // clang-format off
    const std::string expected =
        "/nix/store/hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2\n"
        "\x1B[1m├───/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66\x1B[0m\n"
               "└───/nix/store/6r4zqb04fq5l5l4zghq76wvcpz7dwd35-linux-pam-1.6.1\x1B[0m\n"
        "    \x1B[1m└───/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66\x1B[0m";
    // clang-format on

    auto graph_data =
        aio.blockOn(genGraphString(parent, child, graph, *store, true, false, makeMockAccessor()));
    ASSERT_EQ(graph_data, expected);
}
}
