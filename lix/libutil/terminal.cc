#include "lix/libutil/terminal.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/sync.hh"

#include <sys/ioctl.h>
#include <unistd.h>

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
    auto i = s.begin();

    while (w < (size_t) width && i != s.end()) {

        if (*i == '\e') {
            std::string e;
            e += *i++;
            char last = 0;

            if (i != s.end() && *i == '[') {
                e += *i++;
                // eat parameter bytes
                while (i != s.end() && *i >= 0x30 && *i <= 0x3f) e += *i++;
                // eat intermediate bytes
                while (i != s.end() && *i >= 0x20 && *i <= 0x2f) e += *i++;
                // eat final byte
                if (i != s.end() && *i >= 0x40 && *i <= 0x7e) e += last = *i++;
            } else {
                if (i != s.end() && *i >= 0x40 && *i <= 0x5f) e += *i++;
            }

            if (!filterAll && last == 'm')
                t += e;
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

}
