#include "lix/libutil/canon-path.hh"
#include "lix/libutil/file-system.hh"

namespace nix {

CanonPath CanonPath::root = CanonPath("/");

CanonPath::CanonPath(std::string_view raw)
    : path(absPath((Path) raw, "/"))
{ }

CanonPath::CanonPath(std::string_view raw, const CanonPath & root)
    : path(absPath((Path) raw, root.abs()))
{ }

CanonPath CanonPath::fromCwd(std::string_view path)
{
    return CanonPath(unchecked_t(), absPath((Path) path));
}

std::optional<CanonPath> CanonPath::parent() const
{
    if (isRoot()) return std::nullopt;
    return CanonPath(unchecked_t(), path.substr(0, std::max((size_t) 1, path.rfind('/'))));
}

void CanonPath::extend(const CanonPath & x)
{
    if (x.isRoot()) return;
    if (isRoot())
        path += x.rel();
    else
        path += x.abs();
}

CanonPath CanonPath::operator + (const CanonPath & x) const
{
    auto res = *this;
    res.extend(x);
    return res;
}

void CanonPath::push(std::string_view c)
{
    assert(c.find('/') == c.npos);
    assert(c != "." && c != "..");
    if (!isRoot()) path += '/';
    path += c;
}

CanonPath CanonPath::operator + (std::string_view c) const
{
    auto res = *this;
    res.push(c);
    return res;
}

std::ostream & operator << (std::ostream & stream, const CanonPath & path)
{
    stream << path.abs();
    return stream;
}
}
