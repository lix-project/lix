#include <gtest/gtest.h>
#include "lix/libstore/path-tree.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"

namespace nix {
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
       aio.blockOn(genGraphString(parent, child, graph, *store, false, false));

    // clang-format off
    ASSERT_EQ(
        graph_data,
               "/nix/store/hr8lmmjmd1jk6s3p5ymggyk4am7n2lmb-openssh-10.0p2\n"
        "\x1B[1m└───/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-glibc-2.40-66\x1B[0m"
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
        aio.blockOn(genGraphString(parent, child, graph, *store, true, false));
    ASSERT_EQ(graph_data, expected);
}
}
