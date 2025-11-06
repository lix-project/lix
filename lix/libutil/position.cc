#include "lix/libutil/position.hh"

namespace nix {

std::optional<std::string> Pos::Origin::getSource() const
{
    using OptStr = std::optional<std::string>;

    // clang-format off
    return std::visit(overloaded {
        [](std::monostate const &) -> OptStr {
            return std::nullopt;
        },
        [](Pos::Stdin const & s) -> OptStr {
            return *s.source;
        },
        [](Pos::String const & s) -> OptStr {
            return *s.source;
        },
        [](CheckedSourcePath const & path) -> OptStr {
            try {
                return path.readFile();
            } catch (Error const &) {
                return std::nullopt;
            }
        },
        [](Hidden const &) -> OptStr {
            return std::nullopt;
        },
    }, *this);
    // clang-format on
}

Pos::Pos(const Pos * other)
{
    if (!other) {
        return;
    }
    line = other->line;
    column = other->column;
    origin = std::move(other->origin);
}

Pos::operator std::shared_ptr<Pos>() const
{
    return std::make_shared<Pos>(&*this);
}

std::optional<LinesOfCode> Pos::getCodeLines() const
{
    if (line == 0)
        return std::nullopt;

    if (auto source = getSource()) {
        LinesIterator lines(*source), end;
        LinesOfCode loc;

        if (line > 1)
            std::advance(lines, line - 2);
        if (lines != end && line > 1)
            loc.prevLineOfCode = *lines++;
        if (lines != end)
            loc.errLineOfCode = *lines++;
        if (lines != end)
            loc.nextLineOfCode = *lines++;

        return loc;
    }

    return std::nullopt;
}


std::optional<std::string> Pos::getSource() const
{
    return this->origin.getSource();
}

void Pos::print(std::ostream & out, bool showOrigin) const
{
    if (showOrigin) {
        std::visit(overloaded {
            [&](const std::monostate &) { out << "«none»"; },
            [&](const Pos::Stdin &) { out << "«stdin»"; },
            [&](const Pos::String & s) { out << "«string»"; },
            [&](const SourcePath & path) { out << path; },
            [&](Hidden) { out << "«internal»"; },
        }, origin);
        out << ":";
    }
    out << line;
    if (column > 0)
        out << ":" << column;
}

std::ostream & operator<<(std::ostream & str, const Pos & pos)
{
    pos.print(str, true);
    return str;
}

void Pos::LinesIterator::bump(bool atFirst)
{
    if (!atFirst) {
        pastEnd = input.empty();
        if (!input.empty() && input[0] == '\r')
            input.remove_prefix(1);
        if (!input.empty() && input[0] == '\n')
            input.remove_prefix(1);
    }

    // nix line endings are not only \n as eg std::getline assumes, but also
    // \r\n **and \r alone**. not treating them all the same causes error
    // reports to not match with line numbers as the parser expects them.
    auto eol = input.find_first_of("\r\n");

    if (eol > input.size())
        eol = input.size();

    curLine = input.substr(0, eol);
    input.remove_prefix(eol);
}

}
