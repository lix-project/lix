#include "lix/libutil/terminal.hh"
#include <gtest/gtest.h>
#include <regex>

namespace nix {

TEST(filterANSIEscapes, emptyString) {
    auto s = "";
    auto expected = "";

    ASSERT_EQ(filterANSIEscapes(s), expected);
}

TEST(filterANSIEscapes, doesntChangePrintableChars) {
    auto s = "09 2q304ruyhr slk2-19024 kjsadh sar f";

    ASSERT_EQ(filterANSIEscapes(s), s);
}

TEST(filterANSIEscapes, filtersColorCodes) {
    auto s = "\u001b[30m A \u001b[31m B \u001b[32m C \u001b[33m D \u001b[0m";

    ASSERT_EQ(filterANSIEscapes(s, true, 2), " A" );
    ASSERT_EQ(filterANSIEscapes(s, true, 3), " A " );
    ASSERT_EQ(filterANSIEscapes(s, true, 4), " A  " );
    ASSERT_EQ(filterANSIEscapes(s, true, 5), " A  B" );
    ASSERT_EQ(filterANSIEscapes(s, true, 8), " A  B  C" );
}

TEST(filterANSIEscapes, expandsTabs) {
    auto s = "foo\tbar\tbaz";

    ASSERT_EQ(filterANSIEscapes(s, true), "foo     bar     baz" );
}

TEST(filterANSIEscapes, utf8) {
    ASSERT_EQ(filterANSIEscapes("foobar", true, 5), "fooba");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 6), "fóóbär");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 5), "fóóbä");
    ASSERT_EQ(filterANSIEscapes("fóóbär", true, 3), "fóó");
    ASSERT_EQ(filterANSIEscapes("f€€bär", true, 4), "f€€b");
    ASSERT_EQ(filterANSIEscapes("f𐍈𐍈bär", true, 4), "f𐍈𐍈b");
}

TEST(filterANSIEscapes, stripCSI) {
    EXPECT_EQ(filterANSIEscapes("a\e[1;2;3pb\e[qc"), "abc");
    EXPECT_EQ(filterANSIEscapes("foo\e[0123456789:;<=>? !\"#$%&'()*+,-./~bar\e[@baz"), "foobarbaz");
    // strip malformed sequences too, with parameter bytes after intermediate bytes
    EXPECT_EQ(filterANSIEscapes("foo\e['-';;^bar"), "foobar");
    // strip unfinished sequences
    EXPECT_EQ(filterANSIEscapes("foo\e[123"), "foo");
    // allow colors when !filterAll
    EXPECT_EQ(filterANSIEscapes("foo\e[31;44mbar\e[0m"), "foo\e[31;44mbar\e[0m");
    EXPECT_EQ(filterANSIEscapes("foo\e[31;44mbar\e[0m", true), "foobar");
}

TEST(filterANSIEscapes, undefinedCSI) {
    // if we get an undefined character (outside 0x20–0x7e) behavior is undefined.
    // our current impl will abort the CSI sequence, so this tests for that.
    // it's fine to change that behavior though, and we might want to see what terminals do!
    EXPECT_EQ(filterANSIEscapes("foo\e[123\nbar"), "foo\nbar");
    // if we terminate with \e, ensure we process it for another code
    EXPECT_EQ(filterANSIEscapes("foo\e[123\e[123qbar"), "foobar");
    EXPECT_EQ(filterANSIEscapes("foo\e[123\e[31;44mbar"), "foo\e[31;44mbar");
}

TEST(filterANSIEscapes, stripOSC) {
    // OSC ends with ST (ESC \) or BEL
    EXPECT_EQ(filterANSIEscapes("a\e]0;this is a window title\ab"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e]0;this is a window title\e\\b"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e]\ab\e]\e\\c"), "abc");
    // embedding a CSI in an OSC doesn't confuse things
    EXPECT_EQ(filterANSIEscapes("a\e]\ab\e]\e[31;44m\e\\c"), "abc");
    // parsing ST should not be confused by leading escapes
    EXPECT_EQ(filterANSIEscapes("a\e]0;title\e\e\\b"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e]0;title\e\ab"), "ab");
    // OSC 8 is kept when !filterAll
    EXPECT_EQ(filterANSIEscapes("a \e]8;;http://example.com\e\\link\e]8;;\e\\."), "a \e]8;;http://example.com\e\\link\e]8;;\e\\.");
    EXPECT_EQ(filterANSIEscapes("a \e]8;;http://example.com\e\\link\e]8;;\e\\.", true), "a link.");
    EXPECT_EQ(filterANSIEscapes("a \e]8;id=foo;http://example.com\e\\link\e]8;;\e\\."), "a \e]8;id=foo;http://example.com\e\\link\e]8;;\e\\.");
    // OSC 88 is not OSC 8
    EXPECT_EQ(filterANSIEscapes("a\e]88;;foo\e\\b"), "ab");
    // nor are these variants
    EXPECT_EQ(filterANSIEscapes("a\e];8;foo\e\\b"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e] 8;foo\e\\b"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e]08;foo\e\\b"), "ab");
    EXPECT_EQ(filterANSIEscapes("a\e]]8;foo\e\\b"), "ab");
    // strip unfinished sequences
    EXPECT_EQ(filterANSIEscapes("a\e]0;foo"), "a");
    EXPECT_EQ(filterANSIEscapes("a\e]8;;url"), "a");
}

TEST(filterANSIEscapes, stripCRBEL) {
    using namespace std::string_view_literals;
    // we strip CR and BEL, but not other control characters (besides \e processing and \t
    // expansion). we should probably change this!
    EXPECT_EQ(
        filterANSIEscapes(
            // all control codes except \t and \e
            "a\0\1\2\3\4\5\6\a\b\n\v\f\r\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1c\x1d\x1e\x1f\x7f b"sv
        ),
        "a\0\1\2\3\4\5\6\b\n\v\f\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1c\x1d\x1e\x1f\x7f b"sv
    );
}

TEST(filterANSIEscapes, otherEscapes) {
    // an \e that's not a CSI or OSC eats any number of 0x20–0x2f, plus one more printable
    EXPECT_EQ(filterANSIEscapes("foo\ebar"), "fooar");
    EXPECT_EQ(filterANSIEscapes("foo\e@bar"), "foobar");
    EXPECT_EQ(filterANSIEscapes("foo\e\177bar"), "foobar");
    EXPECT_EQ(filterANSIEscapes("foo\e(Bbar"), "foobar");
    EXPECT_EQ(filterANSIEscapes("foo\e !\"#$%&'()*+,-./qbar"), "foobar");
    // getting control chars in this sequence is undefined, but we process them (except \t)
    EXPECT_EQ(filterANSIEscapes("foo\e\a\r\f\nbar"), "foo\f\nar");
    // this eating aborts on another \e or a \t
    EXPECT_EQ(filterANSIEscapes("foo\e\e[31mbar"), "foo\e[31mbar");
    EXPECT_EQ(filterANSIEscapes("foo\e\tbar", false, std::numeric_limits<unsigned int>::max(), false), "foo\tbar");
    // it also aborts on a utf8 char for simplicity
    EXPECT_EQ(filterANSIEscapes("foo\eƒbar"), "fooƒbar");
}

TEST(filterANSIEscapes, tabs) {
    // eatTabs converts tabs into spaces until tabstop
    EXPECT_EQ(filterANSIEscapes("foo\tbar"), "foo     bar");
    EXPECT_EQ(filterANSIEscapes("\tfoo"), "        foo");
    EXPECT_EQ(filterANSIEscapes("1234567\t"), "1234567 ");
    EXPECT_EQ(filterANSIEscapes("12345678\t"), "12345678        ");
    // filtered escapes don't affect the tabstop
    EXPECT_EQ(filterANSIEscapes("foo\e@\tbar"), "foo     bar");
    EXPECT_EQ(filterANSIEscapes("foo\e[3q\t\e[4pbar"), "foo     bar");
    EXPECT_EQ(filterANSIEscapes("foo\a\r\tbar"), "foo     bar");
    // color/OSC 8 don't either
    EXPECT_EQ(filterANSIEscapes("foo\e[31m\tbar"), "foo\e[31m     bar");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\a\tbar\e]8;;\a"), "foo\e]8;;url\a     bar\e]8;;\a");
    // \e\t still processes the tab
    EXPECT_EQ(filterANSIEscapes("foo\e\tbar"), "foo     bar");
    EXPECT_EQ(filterANSIEscapes("foo\e\tbar", false, std::numeric_limits<unsigned int>::max(), false), "foo\tbar");
    // aborting a CSI with a \t still processes the tab
    EXPECT_EQ(filterANSIEscapes("foo\e[3\tbar"), "foo     bar");
}

TEST(filterANSIEscapes, width) {
    // truncate the string at the given width, ignoring escapes
    EXPECT_EQ(filterANSIEscapes("foo", false, 0), "");
    EXPECT_EQ(filterANSIEscapes("\e[31mfoo", false, 0), "");
    EXPECT_EQ(filterANSIEscapes("foo", false, 1), "f");
    EXPECT_EQ(filterANSIEscapes("\e[31mfoo", false, 1), "\e[31mf");
    EXPECT_EQ(filterANSIEscapes("\a\r\emfoo", false, 1), "f");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\e\\bar\e]8;;\e\\baz", false, 8), "foo\e]8;;url\e\\bar\e]8;;\e\\ba");
    // arguably we should allow kept escapes while we're at the limit, but for now we stop processing
    EXPECT_EQ(filterANSIEscapes("foo\e[31mbar\e][0mbaz", false, 6), "foo\e[31mbar");
    // expanding tabs respects the width
    EXPECT_EQ(filterANSIEscapes("foo\t", false, 4), "foo ");
    EXPECT_EQ(filterANSIEscapes("foo\t", false, 6), "foo   ");
    // truncating with an open OSC 8 closes it if we cut off any OSC 8 codes
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar", false, 4), "foo\e]8;;url\ab");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]8;;\a", false, 4), "foo\e]8;;url\ab\e]8;;\e\\");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\e\\bar\e]8;;other-url\a", false, 4), "foo\e]8;;url\e\\b\e]8;;\e\\");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;id=one;url\abar\e]8;id=two;\a", false, 4), "foo\e]8;id=one;url\ab\e]8;;\e\\");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]8;;\a", false, 3), "foo");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]8;;\a", false, 6), "foo\e]8;;url\abar\e]8;;\e\\");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]8;;\a", false, 7), "foo\e]8;;url\abar\e]8;;\a");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]8;;\abaz", false, 7), "foo\e]8;;url\abar\e]8;;\ab");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;\abar", false, 4), "foo\e]8;;\ab");
    // an OSC 8 with params but no URL we still consider open
    EXPECT_EQ(filterANSIEscapes("foo\e]8;id=one;\abar\e]8;;\a", false, 4), "foo\e]8;id=one;\ab\e]8;;\e\\");
    // we aren't tricked by not-quite-8s
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]88;;\a", false, 4), "foo\e]8;;url\ab");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e] 8;;\a", false, 4), "foo\e]8;;url\ab");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]08;;\a", false, 4), "foo\e]8;;url\ab");
    EXPECT_EQ(filterANSIEscapes("foo\e]8;;url\abar\e]]8;;\a", false, 4), "foo\e]8;;url\ab");
}

TEST(filterANSIEscapes, controlChars) {
    // right now, we keep most control chars, and count them towards width.
    // we should probably change this! but this test shows current behavior.
    EXPECT_EQ(filterANSIEscapes("foo\v\n\fbar", false, 8), "foo\v\n\fba");
}

TEST(makeHyperlink, works)
{
    auto big = std::string(701, 'A');
    EXPECT_EQ(makeHyperlink(big, "meow"), "\e]8;;meow\e\\" + big + "\e]8;;\e\\");
    EXPECT_EQ(makeHyperlink("meow", big), "meow");
}

TEST(makeHyperlinkLocalPath, works)
{
    // NOLINTNEXTLINE(lix-foreign-exceptions): its a test lol
    auto regex = std::regex{R""(^file://([^/]+)/(.*)$)""};
    std::smatch match;
    auto output = makeHyperlinkLocalPath("/a/b/ c", 4);

    ASSERT_TRUE(std::regex_match(output, match, regex));
    // Hostname has a value
    ASSERT_GT(match[1].length(), 0);
    ASSERT_EQ(match[2].str(), "a/b/%20c#4");
}
}
