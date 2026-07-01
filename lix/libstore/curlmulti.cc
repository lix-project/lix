#include "lix/libstore/curlmulti.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/thread-name.hh"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <curl/multi.h>
#include <kj/common.h>

namespace nix {

CurlMulti::CurlMulti(unsigned int baseRetryTimeMs)
    : curlm(curl_multi_init())
    , baseRetryTimeMs(baseRetryTimeMs)
{
    if (curlm == nullptr) {
        throw FileTransferError(FileTransfer::Misc, {}, "could not allocate curl handle");
    }

    static std::once_flag globalInit;
    std::call_once(globalInit, curl_global_init, CURL_GLOBAL_ALL);

    curl_multi_setopt(curlm.get(), CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(curlm.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS,
        fileTransferSettings.httpConnections.get());

    workerThread = std::thread([&]() {
        setCurrentThreadName("curlFileTransfer worker");
        workerThreadEntry();
    });
}

CurlMulti::~CurlMulti()
{
    try {
        stopWorkerThread();
    } catch (nix::Error & e) {
        // This can only fail if a socket to our own process cannot be
        // written to, so it is always a bug in the program if it fails.
        //
        // Joining the thread would probably only cause a deadlock if this
        // happened, so just die on purpose.
        printError("failed to join curl file transfer worker thread: %1%", e.what());
        std::terminate();
    }
    workerThread.join();
}

void CurlMulti::unpause(const std::shared_ptr<TransferItem> & transfer)
{
    auto lock = state_.lock();
    lock->unpause.push_back(transfer);
    wakeup();
}

void CurlMulti::cancel(const std::shared_ptr<TransferItem> & transfer)
{
    std::promise<void> promise;
    auto wait = promise.get_future();
    {
        auto lock = state_.lock();
        if (lock->quit) {
            return;
        }
        lock->cancel[transfer] = std::move(promise);
    }
    wakeup();
    wait.get();
}

void CurlMulti::wakeup()
{
    if (auto mc = curl_multi_wakeup(curlm.get()))
        throw nix::Error("unexpected error from curl_multi_wakeup(): %s", curl_multi_strerror(mc));
}

void CurlMulti::stopWorkerThread()
{
    /* Signal the worker thread to exit. */
    {
        auto state(state_.lock());
        state->quit = true;
    }
    wakeup();
}

void CurlMulti::workerThreadMain()
{
    /* Cause this thread to be notified on SIGINT. */
    auto callback = createInterruptCallback([&]() {
        stopWorkerThread();
    });

    unshareFilesystem();

    std::map<CURL *, std::shared_ptr<TransferItem>> items;

    // clear all current transfers in case of an early exit, as can happen
    // via Interrupted if the interruption occured right before a log call
    KJ_DEFER({
        for (auto & [_, item] : items) {
            item->finish(CURLE_ABORTED_BY_CALLBACK);
        }

        // make a note that we're dying and acknowledge all pending cancel
        // requests by individual transfers. not doing this can cause bugs
        // like #1218 in which the process deadlocks waiting for transfers
        // to cancel with no download thread to make this happen; this was
        // likely caused by a transfer requesting a cancellation *exactly*
        // before a signal was received, causing the curl thread to die in
        // a hurry without processing cancellations. the transfer is stuck
        // from that point on, and since this happened in a destructor the
        // entire process locked up solid. curl exceptions could have also
        // caused this; we set the `quit` flag just in case to avoid this.
        auto lock = state_.lock();
        lock->quit = true;
        for (auto & [item, promise] : lock->cancel) {
            promise.set_value();
        }
    });

    bool quit = false;

    // NOTE: we will need to use CURLMOPT_TIMERFUNCTION to integrate this
    // loop with kj. until then curl will handle its timeouts internally.
    int64_t timeoutMs = INT64_MAX;

    while (true) {
        {
            auto cancel = [&] { return std::exchange(state_.lock()->cancel, {}); }();
            for (auto & [item, promise] : cancel) {
                curl_multi_remove_handle(curlm.get(), item->req.get());
                items.erase(item->req.get());
                promise.set_value();
            }
        }

        /* Let curl do its thing. */
        int running;
        CURLMcode mc = curl_multi_perform(curlm.get(), &running);
        if (mc != CURLM_OK)
            throw nix::Error("unexpected error from curl_multi_perform(): %s", curl_multi_strerror(mc));

        /* Set the promises of any finished requests. */
        CURLMsg * msg;
        int left;
        while ((msg = curl_multi_info_read(curlm.get(), &left))) {
            if (msg->msg == CURLMSG_DONE) {
                auto i = items.find(msg->easy_handle);
                assert(i != items.end());
                i->second->finish(msg->data.result);
                curl_multi_remove_handle(curlm.get(), i->second->req.get());
                items.erase(i);
            }
        }

        // exit immediately and abort all running transfers. waiting for transfers to finish
        // before exiting this loop may hang the shutdown procedure forever, e.g. if blocked
        // transfers would be destroyed (thus aborted) after the curl thread for any reason.
        if (quit) {
            break;
        }

        /* Wait for activity, including wakeup events. */
        mc = curl_multi_poll(curlm.get(), nullptr, 0, std::min<int64_t>(timeoutMs, INT_MAX), nullptr);
        if (mc != CURLM_OK)
            throw nix::Error("unexpected error from curl_multi_poll(): %s", curl_multi_strerror(mc));

        /* Add new curl requests from the incoming requests queue,
           except for requests that are embargoed (waiting for a
           retry timeout to expire). */

        std::vector<std::shared_ptr<TransferItem>> incoming;

        timeoutMs = INT64_MAX;

        {
            auto unpause = [&] { return std::exchange(state_.lock()->unpause, {}); }();
            for (auto & item : unpause) {
                curl_easy_pause(item->req.get(), CURLPAUSE_CONT);
            }
        }

        {
            auto state(state_.lock());
            incoming = std::exchange(state->incoming, {});
            quit = state->quit;
        }

        for (auto & item : incoming) {
            debug("starting %s of %s", item->verb(), item->uri);
            curl_multi_add_handle(curlm.get(), item->req.get());
            items[item->req.get()] = item;
        }
    }

    debug("download thread shutting down");
}

void CurlMulti::workerThreadEntry()
{
    try {
        workerThreadMain();
    } catch (nix::Interrupted & e) {
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        printError("unexpected error in download thread: %s", e.what());
    } catch (...) {
        printError("unexpected error in download thread");
    }

    {
        auto state(state_.lock());
        for (auto & item : state->incoming) {
            item->finish(CURLE_ABORTED_BY_CALLBACK);
        }
        state->incoming.clear();
        state->quit = true;
    }
}

void CurlMulti::enqueueItem(std::shared_ptr<TransferItem> item)
{
    if (item->uploadData
        && !item->uri.starts_with("http://")
        && !item->uri.starts_with("https://"))
        throw nix::Error("uploading to '%s' is not supported", item->uri);

    {
        auto state(state_.lock());
        if (state->quit)
            throw nix::Error("cannot enqueue download request because the download thread is shutting down");
        state->incoming.push_back(item);
    }
    wakeup();
}

} // namespace nix
