#pragma once
///@file

#include "lix/libstore/transferitem.hh"
#include "lix/libutil/sync.hh"

#include <future>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <curl/multi.h>

namespace nix {

struct CurlMulti
{
    // Types.
public:
    struct State
    {
        bool quitting = false;
        bool workAvailable = false;
        std::vector<std::shared_ptr<TransferItem>> incoming;
        std::vector<std::shared_ptr<TransferItem>> unpause;
        std::map<std::shared_ptr<TransferItem>, std::promise<void>> cancel;

        void quit();
    };

    // Fields.
public:
    std::unique_ptr<CURLM, decltype([](auto * m) { curl_multi_cleanup(m); })> curlm;
    const unsigned int baseRetryTimeMs;
    Sync<State> state_;
    std::thread workerThread;

    // Specials.
public:
    CurlMulti(unsigned int baseRetryTimeMs);
    ~CurlMulti();

    // Actual API.
public:
    void unpause(std::shared_ptr<TransferItem> const & transfer);

    void cancel(std::shared_ptr<TransferItem> const & transfer);

    void wakeup(State & locked);

    void stopWorkerThread();

    void workerThreadMain();

    void workerThreadEntry();

    void enqueueItem(std::shared_ptr<TransferItem> item);
};

} // namespace nix
