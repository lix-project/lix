#include "lix/libutil/file-system.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "tests/test-data.hh"

#include <gtest/gtest.h>

#include <numeric>

namespace nix {

/* ----------- tests for libutil ------------------------------------------------*/

    /* ----------------------------------------------------------------------------
     * absPath
     * --------------------------------------------------------------------------*/

    TEST(absPath, doesntChangeRoot) {
        auto p = absPath("/");

        ASSERT_EQ(p, "/");
    }




    TEST(absPath, turnsEmptyPathIntoCWD) {
        char cwd[PATH_MAX+1];
        auto p = absPath("");

        ASSERT_EQ(p, getcwd(cwd, PATH_MAX));
    }

    TEST(absPath, usesOptionalBasePathWhenGiven) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd(_cwd, PATH_MAX);

        auto p = absPath("", cwd);

        ASSERT_EQ(p, cwd);
    }

    TEST(absPath, isIdempotent) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd(_cwd, PATH_MAX);
        auto p1 = absPath(cwd);
        auto p2 = absPath(p1);

        ASSERT_EQ(p1, p2);
    }


    TEST(absPath, pathIsCanonicalised) {
        auto path = "/some/path/with/trailing/dot/.";
        auto p1 = absPath(path);
        auto p2 = absPath(p1);

        ASSERT_EQ(p1, "/some/path/with/trailing/dot");
        ASSERT_EQ(p1, p2);
    }

    /* ----------------------------------------------------------------------------
     * canonPath
     * --------------------------------------------------------------------------*/

    TEST(canonPath, removesTrailingSlashes) {
        auto path = "/this/is/a/path//";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, removesDots) {
        auto path = "/this/./is/a/path/./";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, removesDots2) {
        auto path = "/this/a/../is/a////path/foo/..";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, requiresAbsolutePath) {
        ASSERT_ANY_THROW(canonPath("."));
        ASSERT_ANY_THROW(canonPath(".."));
        ASSERT_ANY_THROW(canonPath("../"));
        ASSERT_ANY_THROW(canonPath(""));
    }

    /* ----------------------------------------------------------------------------
     * dirOf
     * --------------------------------------------------------------------------*/

    TEST(dirOf, returnsEmptyStringForRoot) {
        auto p = dirOf("/");

        ASSERT_EQ(p, "/");
    }

    TEST(dirOf, returnsFirstPathComponent) {
        auto p1 = dirOf("/dir/");
        ASSERT_EQ(p1, "/dir");
        auto p2 = dirOf("/dir");
        ASSERT_EQ(p2, "/");
        auto p3 = dirOf("/dir/..");
        ASSERT_EQ(p3, "/dir");
        auto p4 = dirOf("/dir/../");
        ASSERT_EQ(p4, "/dir/..");
    }

    /* ----------------------------------------------------------------------------
     * baseNameOf
     * --------------------------------------------------------------------------*/

    TEST(baseNameOf, emptyPath) {
        auto p1 = baseNameOf("");
        ASSERT_EQ(p1, "");
    }

    TEST(baseNameOf, pathOnRoot) {
        auto p1 = baseNameOf("/dir");
        ASSERT_EQ(p1, "dir");
    }

    TEST(baseNameOf, relativePath) {
        auto p1 = baseNameOf("dir/foo");
        ASSERT_EQ(p1, "foo");
    }

    TEST(baseNameOf, pathWithTrailingSlashRoot) {
        auto p1 = baseNameOf("/");
        ASSERT_EQ(p1, "");
    }

    TEST(baseNameOf, trailingSlash) {
        auto p1 = baseNameOf("/dir/");
        ASSERT_EQ(p1, "dir");
    }

    /* ----------------------------------------------------------------------------
     * isInDir
     * --------------------------------------------------------------------------*/

    TEST(isInDir, trivialCase) {
        auto p1 = isInDir("/foo/bar", "/foo");
        ASSERT_EQ(p1, true);
    }

    TEST(isInDir, notInDir) {
        auto p1 = isInDir("/zes/foo/bar", "/foo");
        ASSERT_EQ(p1, false);
    }

    // XXX: hm, bug or feature? :) Looking at the implementation
    // this might be problematic.
    TEST(isInDir, emptyDir) {
        auto p1 = isInDir("/zes/foo/bar", "");
        ASSERT_EQ(p1, true);
    }

    /* ----------------------------------------------------------------------------
     * isDirOrInDir
     * --------------------------------------------------------------------------*/

    TEST(isDirOrInDir, trueForSameDirectory) {
        ASSERT_EQ(isDirOrInDir("/nix", "/nix"), true);
        ASSERT_EQ(isDirOrInDir("/", "/"), true);
    }

    TEST(isDirOrInDir, trueForEmptyPaths) {
        ASSERT_EQ(isDirOrInDir("", ""), true);
    }

    TEST(isDirOrInDir, falseForDisjunctPaths) {
        ASSERT_EQ(isDirOrInDir("/foo", "/bar"), false);
    }

    TEST(isDirOrInDir, relativePaths) {
        ASSERT_EQ(isDirOrInDir("/foo/..", "/foo"), true);
    }

    // XXX: while it is possible to use "." or ".." in the
    // first argument this doesn't seem to work in the second.
    TEST(isDirOrInDir, DISABLED_shouldWork) {
        ASSERT_EQ(isDirOrInDir("/foo/..", "/foo/."), true);

    }

    /* ----------------------------------------------------------------------------
     * pathExists
     * --------------------------------------------------------------------------*/

    TEST(pathExists, rootExists) {
        ASSERT_TRUE(pathExists("/"));
    }

    TEST(pathExists, cwdExists) {
        ASSERT_TRUE(pathExists("."));
    }

    TEST(pathExists, bogusPathDoesNotExist) {
        ASSERT_FALSE(pathExists("/schnitzel/darmstadt/pommes"));
    }

    /* ----------------------------------------------------------------------------
     * AutoCloseFD::guessOrInventPath
     * --------------------------------------------------------------------------*/
    void testGuessOrInventPathPrePostDeletion(AutoCloseFD & fd, Path & path) {
        {
            SCOPED_TRACE(fmt("guessing path before deletion of '%1%'", path));
            ASSERT_TRUE(fd);
            /* We cannot predict what the platform will return here.
             * But it cannot fail. */
            ASSERT_TRUE(fd.guessOrInventPath().size() >= 0);
        }
        {
            SCOPED_TRACE(fmt("guessing path after deletion of '%1%'", path));
            deletePath(path);
            /* We cannot predict what the platform will return here.
             * But it cannot fail. */
            ASSERT_TRUE(fd.guessOrInventPath().size() >= 0);
        }
    }
    TEST(guessOrInventPath, files) {
        Path filePath = getUnitTestDataPath("guess-or-invent/test.txt");
        createDirs(dirOf(filePath));
        writeFile(filePath, "some text");
        AutoCloseFD file{open(filePath.c_str(), O_RDONLY, 0666)};
        testGuessOrInventPathPrePostDeletion(file, filePath);
    }

    TEST(guessOrInventPath, directories) {
        Path dirPath = getUnitTestDataPath("guess-or-invent/test-dir");
        createDirs(dirPath);
        AutoCloseFD directory{open(dirPath.c_str(), O_DIRECTORY, 0666)};
        testGuessOrInventPathPrePostDeletion(directory, dirPath);
    }

#ifdef O_PATH
    TEST(guessOrInventPath, symlinks) {
        Path symlinkPath = getUnitTestDataPath("guess-or-invent/test-symlink");
        Path targetPath = getUnitTestDataPath("guess-or-invent/nowhere");
        createDirs(dirOf(symlinkPath));
        createSymlink(targetPath, symlinkPath);
        AutoCloseFD symlink{open(symlinkPath.c_str(), O_PATH | O_NOFOLLOW, 0666)};
        testGuessOrInventPathPrePostDeletion(symlink, symlinkPath);
    }

    TEST(guessOrInventPath, fifos) {
        Path fifoPath = getUnitTestDataPath("guess-or-invent/fifo");
        createDirs(dirOf(fifoPath));
        ASSERT_TRUE(mkfifo(fifoPath.c_str(), 0666) == 0);
        AutoCloseFD fifo{open(fifoPath.c_str(), O_PATH | O_NOFOLLOW, 0666)};
        testGuessOrInventPathPrePostDeletion(fifo, fifoPath);
    }
#endif

    TEST(guessOrInventPath, pipes) {
        int pipefd[2];

        ASSERT_TRUE(pipe(pipefd) == 0);

        AutoCloseFD pipe_read{pipefd[0]};
        ASSERT_TRUE(pipe_read);
        AutoCloseFD pipe_write{pipefd[1]};
        ASSERT_TRUE(pipe_write);

        /* We cannot predict what the platform will return here.
         * But it cannot fail. */
        ASSERT_TRUE(pipe_read.guessOrInventPath().size() >= 0);
        ASSERT_TRUE(pipe_write.guessOrInventPath().size() >= 0);
        pipe_write.close();
        ASSERT_TRUE(pipe_read.guessOrInventPath().size() >= 0);
        pipe_read.close();
    }

    TEST(guessOrInventPath, sockets) {
        Path socketPath = getUnitTestDataPath("guess-or-invent/socket");
        createDirs(dirOf(socketPath));
        AutoCloseFD socket = createUnixDomainSocket(socketPath, 0666);
        testGuessOrInventPathPrePostDeletion(socket, socketPath);
    }

    /* ----------------------------------------------------------------------------
     * concatStringsSep
     * --------------------------------------------------------------------------*/

    TEST(concatStringsSep, buildCommaSeparatedString) {
        Strings strings;
        strings.push_back("this");
        strings.push_back("is");
        strings.push_back("great");

        ASSERT_EQ(concatStringsSep(",", strings), "this,is,great");
    }

    TEST(concatStringsSep, buildStringWithEmptySeparator) {
        Strings strings;
        strings.push_back("this");
        strings.push_back("is");
        strings.push_back("great");

        ASSERT_EQ(concatStringsSep("", strings), "thisisgreat");
    }

    TEST(concatStringsSep, buildSingleString) {
        Strings strings;
        strings.push_back("this");

        ASSERT_EQ(concatStringsSep(",", strings), "this");
    }

    /* ----------------------------------------------------------------------------
     * base64Encode
     * --------------------------------------------------------------------------*/

    TEST(base64Encode, emptyString) {
        ASSERT_EQ(base64Encode(""), "");
    }

    TEST(base64Encode, encodesAString) {
        ASSERT_EQ(base64Encode("quod erat demonstrandum"), "cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0=");
    }

    TEST(base64Encode, encodeAndDecode) {
        auto s = "quod erat demonstrandum";
        auto encoded = base64Encode(s);
        auto decoded = base64Decode(encoded);

        ASSERT_EQ(decoded, s);
    }

    TEST(base64Encode, encodeAndDecodeNonPrintable) {
        char s[256];
        std::iota(std::rbegin(s), std::rend(s), 0);

        auto encoded = base64Encode(s);
        auto decoded = base64Decode(encoded);

        EXPECT_EQ(decoded.length(), 255);
        ASSERT_EQ(decoded, s);
    }

    /* ----------------------------------------------------------------------------
     * base64Decode
     * --------------------------------------------------------------------------*/

    TEST(base64Decode, emptyString) {
        ASSERT_EQ(base64Decode(""), "");
    }

    TEST(base64Decode, decodeAString) {
        ASSERT_EQ(base64Decode("cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0="), "quod erat demonstrandum");
    }

    TEST(base64Decode, decodeThrowsOnInvalidChar) {
        ASSERT_THROW(base64Decode("cXVvZCBlcm_0IGRlbW9uc3RyYW5kdW0="), Error);
    }

    /* ----------------------------------------------------------------------------
     * getLine
     * --------------------------------------------------------------------------*/

    TEST(getLine, all) {
        {
            auto [line, rest] = getLine("foo\nbar\nxyzzy");
            ASSERT_EQ(line, "foo");
            ASSERT_EQ(rest, "bar\nxyzzy");
        }

        {
            auto [line, rest] = getLine("foo\r\nbar\r\nxyzzy");
            ASSERT_EQ(line, "foo");
            ASSERT_EQ(rest, "bar\r\nxyzzy");
        }

        {
            auto [line, rest] = getLine("foo\n");
            ASSERT_EQ(line, "foo");
            ASSERT_EQ(rest, "");
        }

        {
            auto [line, rest] = getLine("foo");
            ASSERT_EQ(line, "foo");
            ASSERT_EQ(rest, "");
        }

        {
            auto [line, rest] = getLine("");
            ASSERT_EQ(line, "");
            ASSERT_EQ(rest, "");
        }
    }

    /* ----------------------------------------------------------------------------
     * toLower
     * --------------------------------------------------------------------------*/

    TEST(toLower, emptyString) {
        ASSERT_EQ(toLower(""), "");
    }

    TEST(toLower, nonLetters) {
        auto s = "!@(*$#)(@#=\\234_";
        ASSERT_EQ(toLower(s), s);
    }

    // std::tolower() doesn't handle unicode characters. In the context of
    // store paths this isn't relevant but doesn't hurt to record this behavior
    // here.
    TEST(toLower, umlauts) {
        auto s = "ÄÖÜ";
        ASSERT_EQ(toLower(s), "ÄÖÜ");
    }

    /* ----------------------------------------------------------------------------
     * string2Float
     * --------------------------------------------------------------------------*/

    TEST(string2Float, emptyString) {
        ASSERT_EQ(string2Float<double>(""), std::nullopt);
    }

    TEST(string2Float, trivialConversions) {
        ASSERT_EQ(string2Float<double>("1.0"), 1.0);

        ASSERT_EQ(string2Float<double>("0.0"), 0.0);

        ASSERT_EQ(string2Float<double>("-100.25"), -100.25);
    }

    /* ----------------------------------------------------------------------------
     * string2Int
     * --------------------------------------------------------------------------*/

    TEST(string2Int, emptyString) {
        ASSERT_EQ(string2Int<int>(""), std::nullopt);
    }

    TEST(string2Int, trivialConversions) {
        ASSERT_EQ(string2Int<int>("1"), 1);

        ASSERT_EQ(string2Int<int>("0"), 0);

        ASSERT_EQ(string2Int<int>("-100"), -100);
    }

    /* ----------------------------------------------------------------------------
     * statusOk
     * --------------------------------------------------------------------------*/

    TEST(statusOk, zeroIsOk) {
        ASSERT_EQ(statusOk(0), true);
        ASSERT_EQ(statusOk(1), false);
    }


    /* ----------------------------------------------------------------------------
     * rewriteStrings
     * --------------------------------------------------------------------------*/

    TEST(rewriteStrings, emptyString) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_EQ(rewriteStrings("", rewrites), "");
    }

    TEST(rewriteStrings, emptyRewrites) {
        StringMap rewrites;

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
    }

    TEST(rewriteStrings, successfulRewrite) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "that and that");
    }

    TEST(rewriteStrings, intransitive) {
        StringMap rewrites;
        // transitivity can happen both in forward and reverse iteration order of the rewrite map.
        rewrites["a"] = "b";
        rewrites["b"] = "c";
        rewrites["e"] = "b";

        ASSERT_EQ(rewriteStrings("abcde", rewrites), "bccdb");
    }

    TEST(rewriteStrings, nonoverlapping) {
        StringMap rewrites;
        rewrites["ab"] = "ca";

        ASSERT_EQ(rewriteStrings("abb", rewrites), "cab");
    }

    TEST(rewriteStrings, differentLength) {
        StringMap rewrites;
        rewrites["a"] = "an has a trea";

        ASSERT_EQ(rewriteStrings("cat", rewrites), "can has a treat");
    }

    TEST(rewriteStrings, sorted) {
        StringMap rewrites;
        rewrites["a"] = "meow";
        rewrites["abc"] = "puppy";

        ASSERT_EQ(rewriteStrings("abcde", rewrites), "meowbcde");
    }

    TEST(rewriteStrings, multiple) {
        StringMap rewrites;
        rewrites["a"] = "b";

        ASSERT_EQ(rewriteStrings("a1a2a3a", rewrites), "b1b2b3b");
    }

    TEST(rewriteStrings, doesntOccur) {
        StringMap rewrites;
        rewrites["foo"] = "bar";

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * replaceStrings
     * --------------------------------------------------------------------------*/

    TEST(replaceStrings, emptyString) {
        ASSERT_EQ(replaceStrings("", "this", "that"), "");
        ASSERT_EQ(replaceStrings("this and that", "", ""), "this and that");
    }

    TEST(replaceStrings, successfulReplace) {
        ASSERT_EQ(replaceStrings("this and that", "this", "that"), "that and that");
    }

    TEST(replaceStrings, doesntOccur) {
        ASSERT_EQ(replaceStrings("this and that", "foo", "bar"), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * trim
     * --------------------------------------------------------------------------*/

    TEST(trim, emptyString) {
        ASSERT_EQ(trim(""), "");
    }

    TEST(trim, removesWhitespace) {
        ASSERT_EQ(trim("foo"), "foo");
        ASSERT_EQ(trim("     foo "), "foo");
        ASSERT_EQ(trim("     foo bar baz"), "foo bar baz");
        ASSERT_EQ(trim("     \t foo bar baz\n"), "foo bar baz");
    }

    /* ----------------------------------------------------------------------------
     * chomp
     * --------------------------------------------------------------------------*/

    TEST(chomp, emptyString) {
        ASSERT_EQ(chomp(""), "");
    }

    TEST(chomp, removesWhitespace) {
        ASSERT_EQ(chomp("foo"), "foo");
        ASSERT_EQ(chomp("foo "), "foo");
        ASSERT_EQ(chomp(" foo "), " foo");
        ASSERT_EQ(chomp(" foo bar baz  "), " foo bar baz");
        ASSERT_EQ(chomp("\t foo bar baz\n"), "\t foo bar baz");
    }

    /* ----------------------------------------------------------------------------
     * quoteStrings
     * --------------------------------------------------------------------------*/

    TEST(quoteStrings, empty) {
        Strings s = { };
        Strings expected = { };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    TEST(quoteStrings, emptyStrings) {
        Strings s = { "", "", "" };
        Strings expected = { "''", "''", "''" };
        ASSERT_EQ(quoteStrings(s), expected);

    }

    TEST(quoteStrings, trivialQuote) {
        Strings s = { "foo", "bar", "baz" };
        Strings expected = { "'foo'", "'bar'", "'baz'" };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    TEST(quoteStrings, quotedStrings) {
        Strings s = { "'foo'", "'bar'", "'baz'" };
        Strings expected = { "''foo''", "''bar''", "''baz''" };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    /* ----------------------------------------------------------------------------
     * tokenizeString
     * --------------------------------------------------------------------------*/

    TEST(tokenizeString, empty) {
        Strings expected = { };

        ASSERT_EQ(tokenizeString<Strings>(""), expected);
    }

    TEST(tokenizeString, tokenizeSpacesWithDefaults) {
        auto s = "foo bar baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsWithDefaults) {
        auto s = "foo\tbar\tbaz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesWithDefaults) {
        auto s = "foo\t bar\t baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesNewlineWithDefaults) {
        auto s = "foo\t\n bar\t\n baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesNewlineRetWithDefaults) {
        auto s = "foo\t\n\r bar\t\n\r baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);

        auto s2 = "foo \t\n\r bar \t\n\r baz";
        Strings expected2 = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s2), expected2);
    }

    TEST(tokenizeString, tokenizeWithCustomSep) {
        auto s = "foo\n,bar\n,baz\n";
        Strings expected = { "foo\n", "bar\n", "baz\n" };

        ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
    }

    /* ----------------------------------------------------------------------------
     * get
     * --------------------------------------------------------------------------*/

    TEST(get, emptyContainer) {
        StringMap s = { };
        auto expected = nullptr;

        ASSERT_EQ(get(s, "one"), expected);
    }

    TEST(get, getFromContainer) {
        StringMap s;
        s["one"] = "yi";
        s["two"] = "er";
        auto expected = "yi";

        ASSERT_EQ(*get(s, "one"), expected);
    }

    TEST(getOr, emptyContainer) {
        StringMap s = { };
        auto expected = "yi";

        ASSERT_EQ(getOr(s, "one", "yi"), expected);
    }

    TEST(getOr, getFromContainer) {
        StringMap s;
        s["one"] = "yi";
        s["two"] = "er";
        auto expected = "yi";

        ASSERT_EQ(getOr(s, "one", "nope"), expected);
    }

    /* ----------------------------------------------------------------------------
     * concatMapStringsSep
     * --------------------------------------------------------------------------*/
    TEST(concatMapStringsSep, empty)
    {
        Strings strings;

        ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "");
    }

    TEST(concatMapStringsSep, justOne)
    {
        Strings strings;
        strings.push_back("this");

        ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "this");
    }

    TEST(concatMapStringsSep, two)
    {
        Strings strings;
        strings.push_back("this");
        strings.push_back("that");

        ASSERT_EQ(concatMapStringsSep(",", strings, [](const std::string & s) { return s; }), "this,that");
    }

    TEST(concatMapStringsSep, map)
    {
        std::map<std::string, std::string> strings;
        strings["this"] = "that";
        strings["1"] = "one";

        ASSERT_EQ(
            concatMapStringsSep(
                ", ", strings, [](const std::pair<std::string, std::string> & s) { return s.first + " -> " + s.second; }),
            "1 -> one, this -> that");
    }

    TEST(ForeignException, typeInfo)
    {
        auto e = [] {
            try {
                // NOLINTNEXTLINE(lix-foreign-exceptions)
                throw std::invalid_argument("foo");
            } catch (...) {
                return ForeignException::wrapCurrent();
            }
        }();
        ASSERT_TRUE(e.is<std::invalid_argument>());
        ASSERT_NE(e.as<std::invalid_argument>(), nullptr);
    }
}
