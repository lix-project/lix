#include "serialise.hh"
#include "charptr-cast.hh"
#include "signals.hh"

#include <cstring>
#include <cerrno>
#include <memory>


namespace nix {


void BufferedSink::operator () (std::string_view data)
{
    if (!buffer) buffer = decltype(buffer)(new char[bufSize]);

    while (!data.empty()) {
        /* Optimisation: bypass the buffer if the data exceeds the
           buffer size. */
        if (bufPos + data.size() >= bufSize) {
            flush();
            writeUnbuffered(data);
            break;
        }
        /* Otherwise, copy the bytes to the buffer.  Flush the buffer
           when it's full. */
        size_t n = bufPos + data.size() > bufSize ? bufSize - bufPos : data.size();
        memcpy(buffer.get() + bufPos, data.data(), n);
        data.remove_prefix(n); bufPos += n;
        if (bufPos == bufSize) flush();
    }
}


void BufferedSink::flush()
{
    if (bufPos == 0) return;
    size_t n = bufPos;
    bufPos = 0; // don't trigger the assert() in ~BufferedSink()
    writeUnbuffered({buffer.get(), n});
}


FdSink::~FdSink()
{
    try { flush(); } catch (...) { ignoreException(); }
}


void FdSink::writeUnbuffered(std::string_view data)
{
    written += data.size();
    try {
        writeFull(fd, data);
    } catch (SysError & e) {
        _good = false;
        throw;
    }
}


bool FdSink::good()
{
    return _good;
}


void Source::operator () (char * data, size_t len)
{
    while (len) {
        size_t n = read(data, len);
        data += n; len -= n;
    }
}


void Source::drainInto(Sink & sink)
{
    std::string s;
    std::array<char, 8192> buf;
    while (true) {
        size_t n;
        try {
            n = read(buf.data(), buf.size());
            sink({buf.data(), n});
        } catch (EndOfFile &) {
            break;
        }
    }
}


std::string Source::drain()
{
    StringSink s;
    drainInto(s);
    return std::move(s.s);
}


size_t BufferedSource::read(char * data, size_t len)
{
    if (!buffer) buffer = decltype(buffer)(new char[bufSize]);

    if (!bufPosIn) bufPosIn = readUnbuffered(buffer.get(), bufSize);

    /* Copy out the data in the buffer. */
    size_t n = len > bufPosIn - bufPosOut ? bufPosIn - bufPosOut : len;
    memcpy(data, buffer.get() + bufPosOut, n);
    bufPosOut += n;
    if (bufPosIn == bufPosOut) bufPosIn = bufPosOut = 0;
    return n;
}


bool BufferedSource::hasData()
{
    return bufPosOut < bufPosIn;
}


size_t FdSource::readUnbuffered(char * data, size_t len)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, data, len);
    } while (n == -1 && errno == EINTR);
    if (n == -1) { _good = false; throw SysError("reading from file"); }
    if (n == 0) { _good = false; throw EndOfFile(std::string(*endOfFileError)); }
    read += n;
    return n;
}


bool FdSource::good()
{
    return _good;
}


size_t StringSource::read(char * data, size_t len)
{
    if (pos == s.size()) throw EndOfFile("end of string reached");
    size_t n = s.copy(data, len, pos);
    pos += n;
    return n;
}


void writePadding(size_t len, Sink & sink)
{
    if (len % 8) {
        char zero[8];
        memset(zero, 0, sizeof(zero));
        sink({zero, 8 - (len % 8)});
    }
}


WireFormatGenerator SerializingTransform::operator()(std::string_view s)
{
    co_yield s.size();
    co_yield Bytes(s.begin(), s.size());
    co_yield SerializingTransform::padding(s.size());
}

WireFormatGenerator SerializingTransform::operator()(const Strings & ss)
{
    co_yield ss.size();
    for (const auto & s : ss)
        co_yield std::string_view(s);
}

WireFormatGenerator SerializingTransform::operator()(const StringSet & ss)
{
    co_yield ss.size();
    for (const auto & s : ss)
        co_yield std::string_view(s);
}

WireFormatGenerator SerializingTransform::operator()(const Error & ex)
{
    auto & info = ex.info();
    co_yield "Error";
    co_yield info.level;
    co_yield "Error"; // removed
    co_yield info.msg.str();
    co_yield 0; // FIXME: info.errPos
    co_yield info.traces.size();
    for (auto & trace : info.traces) {
        co_yield 0; // FIXME: trace.pos
        co_yield trace.hint.str();
    }
}


void readPadding(size_t len, Source & source)
{
    if (len % 8) {
        char zero[8];
        size_t n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


size_t readString(char * buf, size_t max, Source & source)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    source(buf, len);
    readPadding(len, source);
    return len;
}


std::string readString(Source & source, size_t max)
{
    auto len = readNum<size_t>(source);
    if (len > max) throw SerialisationError("string is too long");
    std::string res(len, 0);
    source(res.data(), len);
    readPadding(len, source);
    return res;
}

Source & operator >> (Source & in, std::string & s)
{
    s = readString(in);
    return in;
}


template<class T> T readStrings(Source & source)
{
    auto count = readNum<size_t>(source);
    T ss;
    while (count--)
        ss.insert(ss.end(), readString(source));
    return ss;
}

template Paths readStrings(Source & source);
template PathSet readStrings(Source & source);


Error readError(Source & source)
{
    auto type = readString(source);
    assert(type == "Error");
    auto level = (Verbosity) readInt(source);
    auto name = readString(source); // removed
    auto msg = readString(source);
    ErrorInfo info {
        .level = level,
        .msg = HintFmt(msg),
    };
    auto havePos = readNum<size_t>(source);
    assert(havePos == 0);
    auto nrTraces = readNum<size_t>(source);
    for (size_t i = 0; i < nrTraces; ++i) {
        havePos = readNum<size_t>(source);
        assert(havePos == 0);
        info.traces.push_back(Trace {
            .hint = HintFmt(readString(source))
        });
    }
    return Error(std::move(info));
}


void StringSink::operator () (std::string_view data)
{
    s.append(data);
}

size_t ChainSource::read(char * data, size_t len)
{
    if (useSecond) {
        return source2.read(data, len);
    } else {
        try {
            return source1.read(data, len);
        } catch (EndOfFile &) {
            useSecond = true;
            return this->read(data, len);
        }
    }
}

}
