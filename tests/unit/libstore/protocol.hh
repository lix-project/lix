#pragma once
/// @file
#include <gtest/gtest.h>

#include "lix/libutil/strings.hh"
#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public LibStoreTest
{
protected:
    Path unitTestData = getUnitTestData() + "/libstore/" + protocolDir;

    Path goldenMaster(std::string_view testStem) {
        return unitTestData + "/" + testStem + ".bin";
    }
};

template<class Proto, const char * protocolDir>
class VersionedProtoTest : public ProtoTest<Proto, protocolDir>
{
public:
    /**
     * Golden test for `T` reading
     */
    template<typename T>
    void readTest(PathView testStem, T value)
    {
        if (testAccept())
        {
            GTEST_SKIP() << cannotReadGoldenMaster;
        }
        else
        {
            auto expected = readFile(ProtoTest<Proto, protocolDir>::goldenMaster(testStem));

            T got = ({
                StringSource from { expected };
                Proto::template Serialise<T>::read(typename Proto::ReadConn{from, *LibStoreTest::store});
            });

            ASSERT_EQ(got, value);
        }
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeTest(PathView testStem, const T & value)
    {
        auto file = ProtoTest<Proto, protocolDir>::goldenMaster(testStem);

        StringSink to;
        to << Proto::write(typename Proto::WriteConn{*LibStoreTest::store}, value);

        if (testAccept())
        {
            createDirs(dirOf(file));
            writeFile(file, to.s);
            GTEST_SKIP() << updatingGoldenMaster;
        }
        else
        {
            auto expected = readFile(file);
            ASSERT_EQ(to.s, expected);
        }
    }
};
}
