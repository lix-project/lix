#include "lix/libutil/topo-sort.hh"
#include <gtest/gtest.h>

namespace nix {
static auto testToposort(const std::map<std::string, std::set<std::string>> & data)
{
    std::set<std::string> keys;
    for (auto & [k, _] : data) {
        keys.insert(k);
    }

    return topoSort(keys, {[&](const std::string & lib) { return data.at(lib); }});
}

TEST(toposort, trivial)
{
    // The dependencies are incomplete on purpose here, this is just a test-case.
    auto result = testToposort(
        {{"openssh", {"glibc", "zlib", "polkit"}},
         {"zlib", {"glibc"}},
         {"polkit", {"glibc", "pam"}},
         {"pam", {"glibc"}},
         {"glibc", {}}}
    );

    auto ordered = std::get<std::vector<std::string>>(result);
    ASSERT_EQ(5, ordered.size());

    ASSERT_EQ("openssh", ordered[0]);
    ASSERT_EQ("zlib", ordered[1]);
    ASSERT_EQ("polkit", ordered[2]);
    ASSERT_EQ("pam", ordered[3]);
    ASSERT_EQ("glibc", ordered[4]);
}

TEST(toposort, cycle)
{
    auto result = testToposort({{"foo", {"bar"}}, {"bar", {"baz"}}, {"baz", {"foo"}}});

    auto cycle = std::get<Cycle<std::string>>(result);

    ASSERT_EQ(cycle.path, "bar");
    ASSERT_EQ(cycle.parent, "foo");
}
}
