#include "lix/libutil/closure.hh"
#include "lix/libutil/error.hh"
#include <gtest/gtest.h>

namespace nix {

using namespace std;

map<string, set<string>> testGraph = {
    { "A", { "B", "C", "G" } },
    { "B", { "A" } }, // Loops back to A
    { "C", { "F" } }, // Indirect reference
    { "D", { "A" } }, // Not reachable, but has backreferences
    { "E", {} }, // Just not reachable
    { "F", {} },
    { "G", { "G" } }, // Self reference
};

TEST(closure, correctClosure) {
    set<string> expectedClosure = {"A", "B", "C", "F", "G"};
    set<string> aClosure = computeClosure<string>(
        {"A"},
        [&](const string currentNode) {
            return testGraph[currentNode];
        }
    );

    ASSERT_EQ(aClosure, expectedClosure);
}

TEST(closure, properlyHandlesDirectExceptions) {
    struct TestExn : BaseException {};
    EXPECT_THROW(
        computeClosure<string>(
            {"A"},
            [&](const string currentNode) -> set<string> {
                throw TestExn();
            }
        ),
        TestExn
    );
}

}
