#include <archive.h>
#include <archive_entry.h>
#include <gtest/gtest.h>

#include "lix/libutil/tarfile.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/serialise.hh"

namespace nix {

class TarWriter
{
    std::unique_ptr<struct archive, decltype([](auto * a) { archive_write_free(a); })> archive;
    Sink * sink;

    static int callback_open(struct archive * archive, void * self)
    {
        return ARCHIVE_OK;
    }

    static ssize_t
    callback_write(struct archive * archive, void * self_, const void * buf, size_t len)
    {
        auto * self = static_cast<TarWriter *>(self_);
        try {
            (*self->sink)(std::string_view{static_cast<const char *>(buf), len});
            return len;
        } catch (std::exception & err) { // NOLINT(lix-foreign-exceptions)
            // NOLINTNEXTLINE(lix-unsafe-c-calls): what() is a c string
            archive_set_error(archive, EIO, "Sink threw exception: %s", err.what());
            return -1;
        }
    }

    static int callback_close(struct archive * archive, void * self)
    {
        return ARCHIVE_OK;
    }

    void check(int err, const std::string & reason);

public:
    TarWriter(Sink & sink) : archive(archive_write_new()), sink(&sink)
    {
        check(archive_write_add_filter_gzip(archive.get()), "add filter gzip (%s)");
        check(archive_write_set_format_gnutar(archive.get()), "set format tar (%s)");
        archive_write_set_option(archive.get(), nullptr, "mac-ext", nullptr);

        check(
            archive_write_open(
                archive.get(),
                static_cast<void *>(this),
                callback_open,
                callback_write,
                callback_close
            ),
            "opening archive (%s)"
        );
    }

    void close() &&
    {
        check(archive_write_close(archive.get()), "closing archive (%s)");
    }

    using EntryPtr = std::unique_ptr<struct archive_entry, decltype([](auto * entry) {
                                         archive_entry_free(entry);
                                     })>;

    EntryPtr newEntry()
    {
        return EntryPtr{archive_entry_new()};
    }

    EntryPtr header(const char * path, mode_t mode, unsigned int filetype)
    {
        auto entry = newEntry();
        // NOLINTNEXTLINE(lix-unsafe-c-calls): test code lol
        archive_entry_set_pathname(entry.get(), path);
        archive_entry_set_mode(entry.get(), mode);
        archive_entry_set_filetype(entry.get(), filetype);
        return entry;
    }

    void writeHeader(EntryPtr && entry)
    {
        check(archive_write_header(archive.get(), entry.get()), "write header (%s)");
    }

    void file(const char * path, std::string content, mode_t mode = 0700)
    {
        auto entry = header(path, mode, AE_IFREG);
        archive_entry_set_size(entry.get(), content.length());
        writeHeader(std::move(entry));
        ssize_t written = archive_write_data(archive.get(), content.data(), content.length());
        if (written < 0) {
            check(written, "write data (%s)");
        }
    }

    void dir(const char * path, mode_t mode = 0700)
    {
        auto entry = header(path, mode, AE_IFDIR);
        writeHeader(std::move(entry));
    }

    void symlink(const char * path, const char * target, mode_t mode = 0700)
    {
        auto entry = header(path, mode, AE_IFLNK);
        // NOLINTNEXTLINE(lix-unsafe-c-calls): test code lol
        archive_entry_set_symlink(entry.get(), target);
        writeHeader(std::move(entry));
    }

    void hardlink(const char * path, const char * target, mode_t mode = 0700)
    {
        auto entry = header(path, mode, AE_IFLNK);
        // NOLINTNEXTLINE(lix-unsafe-c-calls): test code lol
        archive_entry_set_hardlink(entry.get(), target);
        writeHeader(std::move(entry));
    }
};

void TarWriter::check(int err, const std::string & reason)
{
    if (err == ARCHIVE_EOF) {
        throw EndOfFile("reached end of archive");
    } else if (err != ARCHIVE_OK) {
        throw Error(reason, archive_error_string(this->archive.get()));
    }
}

/** Abstracted file status operations (for e.g. being able to replace it with a NAR or something
 * else without blowing up all the tests) */
struct FileChecker
{
    Path baseDir;

    bool fileExists(std::string subpath)
    {
        return nix::pathExists(this->baseDir + "/" + subpath);
    }
};

struct TarFixture : testing::Test
{
    Path tmpDir;
    AutoDelete deleter;
    StringSink sink;
    std::unique_ptr<TarWriter> writer;

    TarFixture()
        : tmpDir{createTempDir()}
        , deleter{tmpDir, true}
        , sink{}
        , writer{std::make_unique<TarWriter>(sink)}
    {
    }

    void finish()
    {
        if (writer) {
            std::move(*writer).close();
            writer = nullptr;
        }
    }

    FileChecker extract();
};

FileChecker TarFixture::extract()
{
    finish();

    AsyncIoRoot aio;
    auto stream = AsyncStringInputStream{sink.s};

    aio.blockOn(unpackTarfile(stream, tmpDir));

    return FileChecker{tmpDir};
}

TEST_F(TarFixture, readTrivial)
{
    writer->dir("foo");
    writer->file("foo/bar", "blah");
    auto result = extract();

    ASSERT_TRUE(result.fileExists("foo/bar"));
}

TEST_F(TarFixture, dotdotShouldFail)
{
    writer->dir("../foo");
    writer->file("../foo/bar", "blah");
    writer->dir("bar");
    writer->file("bar/nya", "kitty");

    ASSERT_THROW(extract(), ArchiveError);
}

TEST_F(TarFixture, okayHardlinkWorks)
{
    writer->dir("somedir");
    writer->file("somedir/somefile", "mrrp");
    writer->hardlink("somedir/link", "somedir/somefile");
    auto result = extract();

    ASSERT_TRUE(result.fileExists("somedir/somefile"));
    ASSERT_TRUE(result.fileExists("somedir/link"));
}

TEST_F(TarFixture, badHardlinkOrderFails)
{
    writer->dir("somedir");
    writer->hardlink("somedir/link", "somedir/somefile");
    writer->file("somedir/somefile", "mrrp");

    ASSERT_THROW(extract(), ArchiveError);
}

TEST_F(TarFixture, okaySymlinkWorksIncludingInFunnyOrder)
{
    writer->dir("somedir");
    writer->symlink("somedir/link", "somedir/somefile");
    writer->file("somedir/somefile", "mrrp");
    auto result = extract();

    ASSERT_TRUE(result.fileExists("somedir/somefile"));
    ASSERT_TRUE(result.fileExists("somedir/link"));
}

TEST_F(TarFixture, badFileOnTopOfFile)
{
    writer->dir("somedir");
    writer->file("somedir/file", "ohno");
    writer->file("somedir/file/mrrp", "mrrp");

    ASSERT_THROW(extract(), ArchiveError);
}

TEST_F(TarFixture, badHardlinkTraversalOverFile)
{
    writer->dir("somedir");
    writer->file("somedir/file", "ohno");
    writer->hardlink("somedir/link", "somedir/file/mrrp");

    ASSERT_THROW(extract(), ArchiveError);
}

}
