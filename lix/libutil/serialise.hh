#pragma once
///@file

#include <exception>
#include <kj/async.h>
#include <memory>

#include "error.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/generator.hh"
#include "lix/libutil/io-buffer.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/file-descriptor.hh"

namespace nix {

class AsyncInputStream;

/**
 * Abstract destination of binary data.
 */
struct Sink
{
    virtual ~Sink() { }
    virtual void operator () (std::string_view data) = 0;
    virtual bool good() { return true; }
};

/**
 * Just throws away data.
 */
struct NullSink : Sink
{
    void operator () (std::string_view data) override
    { }
};


struct FinishSink : virtual Sink
{
    virtual void finish() = 0;
};


/**
 * A buffered abstract sink. Warning: a BufferedSink should not be
 * used from multiple threads concurrently.
 */
struct BufferedSink : virtual Sink
{
    ref<IoBuffer> buffer;

    BufferedSink(size_t bufSize = 32 * 1024) : buffer(make_ref<IoBuffer>(bufSize)) {}
    explicit BufferedSink(ref<IoBuffer> buffer) : buffer(std::move(buffer)) {}

    void operator () (std::string_view data) override;

    void flush();

protected:

    virtual void writeUnbuffered(std::string_view data) = 0;
};


/**
 * Abstract source of binary data.
 */
struct Source
{
    virtual ~Source() { }

    /**
     * Store exactly ‘len’ bytes in the buffer pointed to by ‘data’.
     * It blocks until all the requested data is available, or throws
     * an error if it is not going to be available.
     */
    void operator () (char * data, size_t len);

    /**
     * Store up to ‘len’ in the buffer pointed to by ‘data’, and
     * return the number of bytes stored.  It blocks until at least
     * one byte is available.
     *
     * Should not return 0 (generally you want to throw EndOfFile), but nothing
     * stops that.
     *
     * \throws EndOfFile if there is no more data.
     */
    virtual size_t read(char * data, size_t len) = 0;

    void drainInto(Sink & sink);

    std::string drain();
};

/**
 * A buffered abstract source. Warning: a BufferedSource should not be
 * used from multiple threads concurrently.
 */
struct BufferedSource : Source
{
    ref<IoBuffer> buffer;

    BufferedSource(size_t bufSize = 32 * 1024) : buffer(make_ref<IoBuffer>(bufSize)) {}
    explicit BufferedSource(ref<IoBuffer> buffer) : buffer(std::move(buffer)) {}

    size_t read(char * data, size_t len) override;

    bool hasData();

protected:
    /**
     * Underlying read call, to be overridden.
     */
    virtual size_t readUnbuffered(char * data, size_t len) = 0;
};


/**
 * A sink that writes data to a file descriptor.
 */
struct FdSink : BufferedSink
{
    int fd;

    FdSink() : fd(-1) { }
    FdSink(int fd) : fd(fd) { }
    FdSink(int fd, ref<IoBuffer> buffer) : BufferedSink(buffer), fd(fd) {}
    FdSink(const FdSink &) = delete;
    FdSink(FdSink &&) = delete;
    FdSink & operator=(const FdSink &) = delete;
    FdSink & operator=(FdSink &&) = delete;

    ~FdSink();

    void writeUnbuffered(std::string_view data) override;

    bool good() override;

private:
    bool _good = true;
};


/**
 * A source that reads data from a file descriptor.
 */
struct FdSource : BufferedSource
{
    int fd;

    FdSource() : fd(-1) { }
    FdSource(int fd) : fd(fd) { }
    FdSource(int fd, ref<IoBuffer> buffer) : BufferedSource(buffer), fd(fd) {}
    FdSource(const FdSource &) = delete;
    FdSource(FdSource &&) = delete;
    FdSource & operator=(const FdSource &) = delete;
    FdSource & operator=(FdSource &&) = delete;

protected:
    size_t readUnbuffered(char * data, size_t len) override;
};


/**
 * A sink that writes data to a string.
 */
struct StringSink : Sink
{
    std::string s;
    StringSink() { }
    explicit StringSink(const size_t reservedSize)
    {
      s.reserve(reservedSize);
    };
    StringSink(std::string && s) : s(std::move(s)) { };
    void operator () (std::string_view data) override;
};


/**
 * A source that reads data from a string.
 */
struct StringSource : Source
{
    std::string_view s;
    size_t pos;
    StringSource(std::string_view s) : s(s), pos(0) { }
    size_t read(char * data, size_t len) override;
};


/**
 * A sink that writes all incoming data to two other sinks.
 */
struct TeeSink : Sink
{
    Sink & sink1, & sink2;
    TeeSink(Sink & sink1, Sink & sink2) : sink1(sink1), sink2(sink2) { }
    virtual void operator () (std::string_view data) override
    {
        sink1(data);
        sink2(data);
    }
};


/**
 * Adapter class of a Source that saves all data read to a sink.
 */
struct TeeSource : Source
{
    Source & orig;
    Sink & sink;
    TeeSource(Source & orig, Sink & sink)
        : orig(orig), sink(sink) { }
    size_t read(char * data, size_t len) override
    {
        size_t n = orig.read(data, len);
        sink({data, n});
        return n;
    }
};

/**
 * Convert a function into a sink.
 */
struct LambdaSink : Sink
{
    typedef std::function<void(std::string_view data)> lambda_t;

    lambda_t lambda;

    LambdaSink(const lambda_t & lambda) : lambda(lambda) { }

    void operator () (std::string_view data) override
    {
        lambda(data);
    }
};


/**
 * Convert a function into a source.
 */
struct LambdaSource : Source
{
    typedef std::function<size_t(char *, size_t)> lambda_t;

    lambda_t lambda;

    LambdaSource(const lambda_t & lambda) : lambda(lambda) { }

    size_t read(char * data, size_t len) override
    {
        return lambda(data, len);
    }
};

struct GeneratorSource : Source
{
    GeneratorSource(Generator<Bytes> && g) : g(std::move(g)) {}

    virtual size_t read(char * data, size_t len) override
    {
        // we explicitly do not poll the generator multiple times to fill the
        // buffer, only to produce some output at all. this is allowed by the
        // semantics of read(), only operator() must fill the buffer entirely
        while (!buf.size()) {
            if (auto next = g.next()) {
                buf = *next;
            } else {
                throw EndOfFile("coroutine has finished");
            }
        }

        len = std::min(len, buf.size());
        memcpy(data, buf.data(), len);
        buf = buf.subspan(len);
        return len;
    }

private:
    Generator<Bytes> g;
    Bytes buf{};
};

inline Sink & operator<<(Sink & sink, Generator<Bytes> && g)
{
    while (auto buffer = g.next()) {
        sink(std::string_view(buffer->data(), buffer->size()));
    }
    return sink;
}

struct SerializingTransform;
using WireFormatGenerator = Generator<Bytes, SerializingTransform>;

struct SerializingTransform
{
    std::array<unsigned char, 8> buf;

    Bytes operator()(uint64_t n)
    {
        buf[0] = n & 0xff;
        buf[1] = (n >> 8) & 0xff;
        buf[2] = (n >> 16) & 0xff;
        buf[3] = (n >> 24) & 0xff;
        buf[4] = (n >> 32) & 0xff;
        buf[5] = (n >> 40) & 0xff;
        buf[6] = (n >> 48) & 0xff;
        buf[7] = (unsigned char) (n >> 56) & 0xff;
        return {charptr_cast<const char *>(buf.begin()), 8};
    }

    static Bytes padding(size_t unpadded)
    {
        return Bytes("\0\0\0\0\0\0\0", unpadded % 8 ? 8 - unpadded % 8 : 0);
    }

    // opt in to generator chaining. without this co_yielding
    // another generator of any type will cause a type error.
    auto operator()(Generator<Bytes> && g)
    {
        return std::move(g);
    }

    // only choose this for *exactly* char spans, do not allow implicit
    // conversions. this would cause ambiguities with strings literals,
    // and resolving those with more string-like overloads needs a lot.
    template<typename Span>
        requires std::same_as<Span, std::span<char>> || std::same_as<Span, std::span<const char>>
    Bytes operator()(Span s)
    {
        return s;
    }
    WireFormatGenerator operator()(std::string_view s);
    WireFormatGenerator operator()(const Strings & s);
    WireFormatGenerator operator()(const StringSet & s);
    WireFormatGenerator operator()(const Error & s);
};

void writePadding(size_t len, Sink & sink);

// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines):
// These coroutines do their entire job before the semicolon and are not
// retained, so they live long enough.
inline Sink & operator<<(Sink & sink, uint64_t u)
{
    return sink << [&]() -> WireFormatGenerator { co_yield u; }();
}

inline Sink & operator<<(Sink & sink, std::string_view s)
{
    return sink << [&]() -> WireFormatGenerator { co_yield s; }();
}

inline Sink & operator<<(Sink & sink, const Strings & s)
{
    return sink << [&]() -> WireFormatGenerator { co_yield s; }();
}

inline Sink & operator<<(Sink & sink, const StringSet & s)
{
    return sink << [&]() -> WireFormatGenerator { co_yield s; }();
}

inline Sink & operator<<(Sink & sink, const Error & ex)
{
    return sink << [&]() -> WireFormatGenerator { co_yield ex; }();
}
// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)

MakeError(SerialisationError, Error);

template<typename T>
T readNum(Source & source);
template<typename T>
kj::Promise<Result<T>> readNum(AsyncInputStream & source);

void readPadding(size_t len, Source & source);
kj::Promise<Result<void>> readPadding(size_t len, AsyncInputStream & source);

std::string readString(Source & source, size_t max = std::numeric_limits<size_t>::max());
kj::Promise<Result<std::string>>
readString(AsyncInputStream & source, size_t max = std::numeric_limits<size_t>::max());

template<class T>
T readStrings(Source & source);
template<class T>
kj::Promise<Result<T>> readStrings(AsyncInputStream & source);

inline bool readBool(Source & in)
{
    return readNum<uint64_t>(in);
}

inline kj::Promise<Result<bool>> readBool(AsyncInputStream & in)
try {
    co_return LIX_TRY_AWAIT(readNum<uint64_t>(in));
} catch (...) {
    co_return result::current_exception();
}

Error readError(Source & source);
kj::Promise<Result<Error>> readError(AsyncInputStream & source);

/**
 * An adapter that converts a std::basic_istream into a source.
 */
struct StreamToSourceAdapter : Source
{
    std::shared_ptr<std::basic_istream<char>> istream;

    StreamToSourceAdapter(std::shared_ptr<std::basic_istream<char>> istream)
        : istream(istream)
    { }

    size_t read(char * data, size_t len) override
    {
        if (!istream->read(data, len)) {
            if (istream->eof()) {
                if (istream->gcount() == 0)
                    throw EndOfFile("end of file");
            } else
                throw Error("I/O error in StreamToSourceAdapter");
        }
        return istream->gcount();
    }
};


/**
 * A source that reads a distinct format of concatenated chunks back into its
 * logical form, in order to guarantee a known state to the original stream,
 * even in the event of errors.
 *
 * Use with FramedSink, which also allows the logical stream to be terminated
 * in the event of an exception.
 */
struct FramedSource : Source
{
    Source & from;
    bool eof = false;
    std::vector<char> pending;
    size_t pos = 0;

    FramedSource(Source & from) : from(from)
    { }

    ~FramedSource()
    {
        try {
            if (!eof) {
                while (true) {
                    auto n = readNum<unsigned>(from);
                    if (!n) break;
                    std::vector<char> data(n);
                    from(data.data(), n);
                }
            }
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }

    size_t read(char * data, size_t len) override
    {
        if (eof) throw EndOfFile("reached end of FramedSource");

        if (pos >= pending.size()) {
            size_t len = readNum<unsigned>(from);
            if (!len) {
                eof = true;
                return 0;
            }
            pending = std::vector<char>(len);
            pos = 0;
            from(pending.data(), len);
        }

        auto n = std::min(len, pending.size() - pos);
        memcpy(data, pending.data() + pos, n);
        pos += n;
        return n;
    }
};
}
