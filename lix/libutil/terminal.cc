#include "lix/libutil/terminal.hh"
#include "fmt.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/sync.hh"
#include "url.hh"

#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>

namespace nix {

bool isOutputARealTerminal(StandardOutputStream fileno)
{
    return isatty(int(fileno)) && getEnv("TERM").value_or("dumb") != "dumb";
}

bool shouldANSI(StandardOutputStream fileno)
{
    // Implements the behaviour described by https://bixense.com/clicolors/
    // As well as https://force-color.org/ for compatibility, since it fits in the same shape.
    // NO_COLOR CLICOLOR CLICOLOR_FORCE Colours?
    // set      x        x              No
    // unset    x        set            Yes
    // unset    x        unset          If attached to a terminal
    //                                  [we choose the "modern" approach of colour-by-default]
    auto compute = [](StandardOutputStream fileno) -> bool {
        bool mustNotColour = getEnv("NO_COLOR").has_value() || getEnv("NOCOLOR").has_value();
        bool shouldForce = getEnv("CLICOLOR_FORCE").has_value() || getEnv("FORCE_COLOR").has_value();
        bool isTerminal = isOutputARealTerminal(fileno);
        return !mustNotColour && (shouldForce || isTerminal);
    };
    static bool cached[2] = {compute(StandardOutputStream::Stdout), compute(StandardOutputStream::Stderr)};
    return cached[int(fileno) - 1];
}

// FIXME(jade): replace with TerminalCodeEater. wowie this is evil code.
std::string filterANSIEscapes(std::string_view s, bool filterAll, unsigned int width, bool eatTabs)
{
    std::string t;
    size_t w = 0;
    bool inHyperlink = false;
    auto i = s.begin();

    while (w < (size_t) width && i != s.end()) {

        if (*i == '\e') {
            std::string e;
            e += *i++;

            if (i != s.end() && *i == '[') { // CSI sequence
                e += *i++;
                // CSI is terminated by a byte in the range 0x40–0x7e.
                // Behavior is undefined if we get a byte outside 0x20–0x7e.
                // We don't care about the exact format of the parameters, just that we find the
                // end of the sequence, so we'll stop on an invalid byte.
                char last = 0;

                // eat parameter / intermediate bytes
                while (i != s.end() && *i >= 0x20 && *i <= 0x3f) e += *i++;
                // eat terminator byte
                if (i != s.end() && *i >= 0x40 && *i <= 0x7e) e += last = *i++;

                // print colors if enabled
                if (!filterAll && last == 'm')
                    t += e;
            } else if (i != s.end() && *i == ']') { // OSC sequence
                e += *i++;
                // OSC is terminated by ST (\e\\).
                // For historical reasons it can also be ended with BEL (\a).
                // We only care about OSC 8, hyperlinks
                char ps = 0;

                // eat first parameter
                if (i != s.end() && *i >= 0x30 && *i <= 0x3f) e += ps = *i++;
                if (!(i != s.end() && *i == ';')) ps = 0; // not a single-digit parameter
                // eat until ST
                while (true) {
                    if (i == s.end()) {
                        ps = 0; // don't print unfinished sequences
                        break;
                    }
                    char c;
                    e += c = *i++;
                    if (c == '\a') break;
                    if (c == '\e' && i != s.end() && *i == '\\') {
                        e += *i++;
                        break;
                    }
                }

                // print OSC 8 if enabled
                if (!filterAll && ps == '8') {
                    inHyperlink = !(e == "\e]8;;\a" || e == "\e]8;;\e\\");
                    t += e;
                }
            } else {
                // some other escape. Most of these are just one byte, but the nF escapes can have
                // multiple bytes in 0x20–0x2f before the terminator. Getting something outside
                // 0x20–0x7e at this point is undefined but experimentally it seems some terminals
                // process control chars without interrupting the sequence. We'll abort on
                // non-ASCII though for simplicity, and \t so we can expand it.
                while (i != s.end() && *i != '\e' && *i != '\t') {
                    if (*i & 0x80) break; // utf8 byte
                    else if (*i >= 0x30) {
                        // terminator byte
                        i++;
                        break;
                    } else if (*i >= 0x20) {
                        // nF escape continuation byte
                        i++;
                    } else if (*i == '\r' || *i == '\a') {
                        // escapes that we ignore down below
                        i++;
                    } else {
                        // down below we don't check for other control chars, so we treat them as
                        // printable chars. we should probably change that, but for now just match
                        // the behavior.
                        t += *i++;
                        if (++w >= (size_t)width) break;
                    }
                }
            }

        }

        else if (*i == '\t' && eatTabs) {
            i++; t += ' '; w++;
            while (w < (size_t) width && w % 8) {
                t += ' '; w++;
            }
        }

        else if (*i == '\r' || *i == '\a')
            // do nothing for now
            i++;

        else {
            w++;
            // Copy one UTF-8 character.
            if ((*i & 0xe0) == 0xc0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
            } else if ((*i & 0xf0) == 0xe0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                }
            } else if ((*i & 0xf8) == 0xf0) {
                t += *i++;
                if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                    t += *i++;
                    if (i != s.end() && ((*i & 0xc0) == 0x80)) {
                        t += *i++;
                        if (i != s.end() && ((*i & 0xc0) == 0x80)) t += *i++;
                    }
                }
            } else
                t += *i++;
        }
    }

    // if we truncated with an open OSC 8, check if we're throwing away another OSC 8 and, if so,
    // close it. Our callers know to reset colors but probably don't know to reset hyperlink.
    if (inHyperlink) {
        char next = '\e';
        while (i != s.end()) {
            char c = *i++;
            if (c == '\e') next = ']';
            else if (c == next) {
                if (next == ']') next = '8';
                else if (next == '8') next = ';';
                else if (next == ';') {
                    // that's enough to identify an OSC 8
                    t += "\e]8;;\e\\";
                    break;
                }  else next = '\e';
            } else next = '\e';
        }
    }

    return t;
}

static Sync<std::pair<unsigned short, unsigned short>> windowSize{{0, 0}};

void updateWindowSize()
{
    struct winsize ws;
    if (ioctl(2, TIOCGWINSZ, &ws) == 0 || ioctl(1, TIOCGWINSZ, &ws) == 0) {
        auto windowSize_(windowSize.lock());
        windowSize_->first = ws.ws_row;
        windowSize_->second = ws.ws_col;
    }
}


std::pair<unsigned short, unsigned short> getWindowSize()
{
    return *windowSize.lock();
}

std::string makeHyperlink(std::string_view linkText, std::string_view target)
{
    // 700 is arbitrarily chosen as a length limit as it's where screen breaks
    // according to https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda#length-limits
    if (target.empty() || target.length() > 700) {
        return std::string{linkText};
    }

#define OSC "\e]"
#define ST "\e\\"

    return fmt(OSC "8;;%s" ST "%s" OSC "8;;" ST, target, linkText);

#undef OSC
#undef ST
}

std::string makeHyperlinkLocalPath(std::string_view path, std::optional<unsigned> lineNumber)
{
    // File paths in OSC 8 are required to have the hostname in them per the
    // spec.
    static std::string theHostname = []() -> std::string {
        // According to POSIX if the hostname is too long, there is no guarantee of
        // null termination so let's make sure there's always one.
        char theHostname_[_POSIX_HOST_NAME_MAX + 1] = {};

        int err = gethostname(theHostname_, sizeof(theHostname_) - 1);
        // Who knows why getting the hostname would fail, but it is fallible!
        if (err < 0) {
            return "localhost";
        } else {
            return theHostname_;
        }
    }();

    if (!path.starts_with('/')) {
        // Problematic to have non absolute paths
        return "";
    }
    auto content = percentEncode(path, "/");

    // XXX(jade): these schemes are not standardized and even the file link
    // line number has no guarantee to work (and in fact theoretically is
    // supported in kitty but in practice is mostly ignored).
    // https://github.com/BurntSushi/ripgrep/blob/bf63fe8f258afc09bae6caa48f0ae35eaf115005/crates/printer/src/hyperlink_aliases.rs#L4-L22
    auto result = fmt("file://%s%s", theHostname, content);
    if (lineNumber.has_value()) {
        result += fmt("#%d", *lineNumber);
    }
    return result;
}
}
