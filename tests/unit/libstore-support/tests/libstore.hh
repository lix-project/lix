#pragma once
///@file

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"

namespace nix {

class LibStoreTest : public ::testing::Test {
    public:
        static void SetUpTestSuite() {
            initLibStore();
        }

    protected:
        LibStoreTest()
            : store(aio.blockOn(openStore("dummy://")))
        { }

        ~LibStoreTest() noexcept(true) {}

        AsyncIoRoot aio;
        ref<Store> store;
};


} /* namespace nix */
