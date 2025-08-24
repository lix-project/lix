#include "async-io.hh"
#include "async.hh"
#include "box_ptr.hh"
#include "error.hh"
#include "file-descriptor.hh"
#include "io-buffer.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/tarfile.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/logging.hh"
#include "result.hh"
#include "serialise.hh"

#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <exception>
#include <fcntl.h>
#include <future>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/exception.h>
#include <memory>

namespace nix {

static const int COMPRESSION_LEVEL_DEFAULT = -1;

// Don't feed brotli too much at once.
struct ChunkedCompressionSink : CompressionSink
{
    uint8_t outbuf[32 * 1024];

    void writeUnbuffered(std::string_view data) override
    {
        const size_t CHUNK_SIZE = sizeof(outbuf) << 2;
        while (!data.empty()) {
            size_t n = std::min(CHUNK_SIZE, data.size());
            writeInternal(data.substr(0, n));
            data.remove_prefix(n);
        }
    }

    virtual void writeInternal(std::string_view data) = 0;
};

struct ArchiveDecompressionSource : Source
{
    std::unique_ptr<TarArchive> archive = 0;
    std::unique_ptr<Source> src;
    ArchiveDecompressionSource(std::unique_ptr<Source> src) : src(std::move(src)) {}
    ~ArchiveDecompressionSource() override {}
    size_t read(char * data, size_t len) override {
        struct archive_entry * ae;
        if (!archive) {
            archive = std::make_unique<TarArchive>(*src, true);
            this->archive->check(archive_read_next_header(this->archive->archive, &ae),
                "failed to read header (%s)");
            if (archive_filter_count(this->archive->archive) < 2) {
                throw CompressionError("input compression not recognized");
            }
        }
        ssize_t result = archive_read_data(this->archive->archive, data, len);
        if (result > 0) return result;
        if (result == 0) {
            throw EndOfFile("reached end of compressed file");
        }
        this->archive->check(result, "failed to read compressed data (%s)");
        return result;
    }
};

struct ArchiveCompressionSink : CompressionSink
{
    Sink & nextSink;
    struct archive * archive;

    ArchiveCompressionSink(Sink & nextSink, std::string format, bool parallel, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        archive = archive_write_new();
        if (!archive) throw Error("failed to initialize libarchive");
        check(archive_write_add_filter_by_name(archive, format.c_str()), "couldn't initialize compression (%s)");
        check(archive_write_set_format_raw(archive));
        if (parallel)
            check(archive_write_set_filter_option(archive, format.c_str(), "threads", "0"));
        if (level != COMPRESSION_LEVEL_DEFAULT)
            check(archive_write_set_filter_option(archive, format.c_str(), "compression-level", std::to_string(level).c_str()));
        // disable internal buffering
        check(archive_write_set_bytes_per_block(archive, 0));
        // disable output padding
        check(archive_write_set_bytes_in_last_block(archive, 1));
        open();
    }

    ~ArchiveCompressionSink() override
    {
        if (archive) archive_write_free(archive);
    }

    void finish() override
    {
        flush();
        check(archive_write_close(archive));
    }

    void check(int err, const std::string & reason = "failed to compress (%s)")
    {
        if (err == ARCHIVE_EOF)
            throw EndOfFile("reached end of archive");
        else if (err != ARCHIVE_OK)
            throw Error(reason, archive_error_string(this->archive));
    }

    void writeUnbuffered(std::string_view data) override
    {
        ssize_t result = archive_write_data(archive, data.data(), data.length());
        if (result <= 0) check(result);
    }

private:
    void open()
    {
        check(archive_write_open(archive, this, nullptr, ArchiveCompressionSink::callback_write, nullptr));
        auto ae = archive_entry_new();
        archive_entry_set_filetype(ae, AE_IFREG);
        check(archive_write_header(archive, ae));
        archive_entry_free(ae);
    }

    static ssize_t callback_write(struct archive * archive, void * _self, const void * buffer, size_t length)
    {
        auto self = static_cast<ArchiveCompressionSink *>(_self);
        self->nextSink({static_cast<const char *>(buffer), length});
        return length;
    }
};

struct NoneSink : CompressionSink
{
    Sink & nextSink;
    NoneSink(Sink & nextSink, int level = COMPRESSION_LEVEL_DEFAULT) : nextSink(nextSink)
    {
        if (level != COMPRESSION_LEVEL_DEFAULT)
            printTaggedWarning(
                "requested compression level '%d' not supported by compression method 'none'", level
            );
    }
    void finish() override { flush(); }
    void writeUnbuffered(std::string_view data) override { nextSink(data); }
};

struct BrotliDecompressionSource : Source
{
    static constexpr size_t BUF_SIZE = 32 * 1024;
    std::unique_ptr<char[]> buf;
    size_t avail_in = 0;
    const uint8_t * next_in;
    std::exception_ptr inputEofException = nullptr;

    std::unique_ptr<Source> inner;
    std::unique_ptr<BrotliDecoderState, void (*)(BrotliDecoderState *)> state;

    BrotliDecompressionSource(std::unique_ptr<Source> inner)
        : buf(std::make_unique<char[]>(BUF_SIZE))
        , inner(std::move(inner))
        , state{
              BrotliDecoderCreateInstance(nullptr, nullptr, nullptr), BrotliDecoderDestroyInstance
          }
    {
        if (!state) {
            throw CompressionError("unable to initialize brotli decoder");
        }
    }

    size_t read(char * data, size_t len) override
    {
        uint8_t * out = charptr_cast<uint8_t *>(data);
        const auto * begin = out;

        while (len && !BrotliDecoderIsFinished(state.get())) {
            checkInterrupt();

            while (avail_in == 0 && inputEofException == nullptr) {
                try {
                    avail_in = inner->read(buf.get(), BUF_SIZE);
                } catch (EndOfFile &) {
                    // No more data, but brotli may still have output remaining
                    // from the last call.
                    inputEofException = std::current_exception();
                    break;
                }
                next_in = charptr_cast<const uint8_t *>(buf.get());
            }

            BrotliDecoderResult res = BrotliDecoderDecompressStream(
                state.get(), &avail_in, &next_in, &len, &out, nullptr
            );

            switch (res) {
            case BROTLI_DECODER_RESULT_SUCCESS:
                // We're done here!
                goto finish;
            case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
                // Grab more input. Don't try if we already have exhausted our input stream.
                if (inputEofException != nullptr) {
                    std::rethrow_exception(inputEofException);
                } else {
                    continue;
                }
            case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
                // Need more output space: we can only get another buffer by someone calling us again, so get out.
                goto finish;
            case BROTLI_DECODER_RESULT_ERROR:
                throw CompressionError("error while decompressing brotli file");
            }
        }

finish:
        if (begin != out) {
            return out - begin;
        } else {
            throw EndOfFile("brotli stream exhausted");
        }
    }
};

std::string decompress(const std::string & method, std::string_view in)
{
    auto filter = makeDecompressionSource(method, std::make_unique<StringSource>(in));
    return filter->drain();
}

std::unique_ptr<Source>
makeDecompressionSource(const std::string & method, std::unique_ptr<Source> inner)
{
    if (method == "none" || method == "") {
        return inner;
    } else if (method == "br") {
        return std::make_unique<BrotliDecompressionSource>(std::move(inner));
    } else {
        return std::make_unique<ArchiveDecompressionSource>(std::move(inner));
    }
}

namespace {
struct DecompressorPipes
{
    Pipe compressed, uncompressed;
    std::optional<kj::UnixEventPort::FdObserver> writeObserver;
    std::optional<kj::UnixEventPort::FdObserver> readObserver;

    DecompressorPipes()
    {
        compressed.create();
        uncompressed.create();

        writeObserver.emplace(
            AIO().unixEventPort,
            compressed.writeSide.get(),
            kj::UnixEventPort::FdObserver::OBSERVE_WRITE
        );
        readObserver.emplace(
            AIO().unixEventPort,
            uncompressed.readSide.get(),
            kj::UnixEventPort::FdObserver::OBSERVE_READ
        );
    }
};

// since we do not have any good async decompression libraries, especially none that behave
// like libarchive, we must make async decompression an adaptor for sync decompression. the
// least painful way to do this is two pipe pairs and a thread that handles the synchronous
// bit. care must be taken to clear kj fd observer before closing the corresponding fds; if
// we close the fds first kj will throw an EBADFD exception. we use a feeder promise in the
// background to copy data from the inner stream to the decompressor, this promise too must
// be cancelled before we close any of our file descriptors. decompression errors are moved
// from the thread to the main user via a `std::async` future and (its result) during read.
//
// this is easier to write than userspace-only pipes and involves marginally more syscalls,
// but those few are unavoidable *anyway* (or we might starve other promises in the system)
struct DecompressionStream : DecompressorPipes, AsyncInputStream
{
    box_ptr<AsyncInputStream> inner;
    std::unique_ptr<FdSink> sink;
    std::unique_ptr<Source> decompressor;
    std::future<void> thread;
    std::exception_ptr feedExc;
    kj::Promise<void> feeder = nullptr;

    DecompressionStream(const std::string & method, box_ptr<AsyncInputStream> inner)
        : inner(std::move(inner))
    {
        makeNonBlocking(compressed.writeSide.get());
        makeNonBlocking(uncompressed.readSide.get());

        sink = std::make_unique<FdSink>(uncompressed.writeSide.get());
        decompressor =
            makeDecompressionSource(method, std::make_unique<FdSource>(compressed.readSide.get()));

        thread = std::async(std::launch::async, [&] {
            // signal the feeder and reader when we're done
            KJ_DEFER({
                uncompressed.writeSide.close();
                compressed.readSide.close();
            });
            decompressor->drainInto(*sink);
            sink->flush();
        });
        feeder = feed().eagerlyEvaluate([&](auto e) {
            feedExc = std::make_exception_ptr(std::move(e));
            compressed.writeSide.close();
        });
    }

    ~DecompressionStream()
    {
        feeder = nullptr;
        readObserver.reset();
        writeObserver.reset();
        // have the decompressor thread exit
        compressed.writeSide.close();
        uncompressed.readSide.close();
        // don't poll the decompressor future, we don't want the error.
        // we just want it to be gone so ~future doesn't block forever.
    }

    kj::Promise<void> feed()
    try {
        KJ_DEFER({
            // signal the decompressor thread that we're done
            writeObserver.reset();
            compressed.writeSide.close();
        });
        // buffer size chosen by lifting the maximum from kj decompression wrappers
        IoBuffer buf{8192};
        while (true) {
            if (buf.used() == 0) {
                const auto space = buf.getWriteBuffer();
                const auto got = TRY_AWAIT(inner->read(space.data(), space.size()));
                if (got) {
                    buf.added(*got);
                } else {
                    co_return;
                }
            }

            while (buf.used() > 0) {
                const auto available = buf.getReadBuffer();
                const auto wrote =
                    ::write(compressed.writeSide.get(), available.data(), available.size());
                if (wrote >= 0) {
                    buf.consumed(wrote);
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    co_await writeObserver->whenBecomesWritable();
                } else if (errno == EPIPE) {
                    co_return;
                } else {
                    throw SysError("feeding decompression stream");
                }
            }
        }
    } catch (...) {
        feedExc = std::current_exception();
    }

    kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
    try {
        while (true) {
            if (const auto got = ::read(uncompressed.readSide.get(), buffer, size); got > 0) {
                co_return got;
            } else if (got == 0) {
                // decompresser must have finished, poll for any errors and return EOF
                thread.get();
                co_return std::nullopt;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                co_await readObserver->whenBecomesReadable();
            } else {
                throw SysError("reading decompression stream");
            }
        }
    } catch (...) {
        co_return result::current_exception();
    }
};
}

box_ptr<AsyncInputStream>
makeDecompressionStream(const std::string & method, box_ptr<AsyncInputStream> inner)
{
    return make_box_ptr<DecompressionStream>(method, std::move(inner));
}

struct BrotliCompressionSink : ChunkedCompressionSink
{
    Sink & nextSink;
    uint8_t outbuf[BUFSIZ];
    BrotliEncoderState * state;
    bool finished = false;

    BrotliCompressionSink(Sink & nextSink) : nextSink(nextSink)
    {
        state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state)
            throw CompressionError("unable to initialise brotli encoder");
    }

    ~BrotliCompressionSink()
    {
        BrotliEncoderDestroyInstance(state);
    }

    void finish() override
    {
        flush();
        writeInternal({});
    }

    void writeInternal(std::string_view data) override
    {
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        auto next_in = charptr_cast<const uint8_t *>(data.data());
        size_t avail_in = data.size();
        uint8_t * next_out = outbuf;
        size_t avail_out = sizeof(outbuf);

        while (!finished && (!data.data() || avail_in)) {
            checkInterrupt();

            if (!BrotliEncoderCompressStream(state,
                    data.data() ? BROTLI_OPERATION_PROCESS : BROTLI_OPERATION_FINISH,
                    &avail_in, &next_in,
                    &avail_out, &next_out,
                    nullptr))
                throw CompressionError("error while compressing brotli compression");

            if (avail_out < sizeof(outbuf) || avail_in == 0) {
                nextSink({reinterpret_cast<const char *>(outbuf), sizeof(outbuf) - avail_out});
                next_out = outbuf;
                avail_out = sizeof(outbuf);
            }

            finished = BrotliEncoderIsFinished(state);
        }
    }
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel, int level)
{
    std::vector<std::string> la_supports = {
        "bzip2", "compress", "grzip", "gzip", "lrzip", "lz4", "lzip", "lzma", "lzop", "xz", "zstd"
    };
    if (std::find(la_supports.begin(), la_supports.end(), method) != la_supports.end()) {
        return make_ref<ArchiveCompressionSink>(nextSink, method, parallel, level);
    }
    if (method == "none")
        return make_ref<NoneSink>(nextSink);
    else if (method == "br")
        return make_ref<BrotliCompressionSink>(nextSink);
    else
        throw UnknownCompressionMethod("unknown compression method '%s'", method);
}

std::string compress(const std::string & method, std::string_view in, const bool parallel, int level)
{
    StringSink ssink;
    auto sink = makeCompressionSink(method, ssink, parallel, level);
    (*sink)(in);
    sink->finish();
    return std::move(ssink.s);
}

}
