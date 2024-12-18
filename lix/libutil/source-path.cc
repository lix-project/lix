#include "lix/libutil/source-path.hh"
#include "lix/libutil/strings.hh"

namespace nix {

std::ostream & operator << (std::ostream & str, const SourcePath & path)
{
    str << path.to_string();
    return str;
}

std::string_view SourcePath::baseName() const
{
    return path.baseName().value_or("source");
}

SourcePath SourcePath::parent() const
{
    auto p = path.parent();
    assert(p);
    return std::move(*p);
}

InputAccessor::Stat SourcePath::lstat() const
{
    auto st = nix::lstat(path.abs());
    return InputAccessor::Stat {
        .type =
            S_ISREG(st.st_mode) ? InputAccessor::tRegular :
            S_ISDIR(st.st_mode) ? InputAccessor::tDirectory :
            S_ISLNK(st.st_mode) ? InputAccessor::tSymlink :
            InputAccessor::tMisc,
        .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
    };
}

std::optional<InputAccessor::Stat> SourcePath::maybeLstat() const
{
    // FIXME: merge these into one operation.
    if (!pathExists())
        return {};
    return lstat();
}

InputAccessor::DirEntries SourcePath::readDirectory() const
{
    InputAccessor::DirEntries res;
    for (auto & entry : nix::readDirectory(path.abs())) {
        std::optional<InputAccessor::Type> type;
        switch (entry.type) {
        case DT_REG: type = InputAccessor::Type::tRegular; break;
        case DT_LNK: type = InputAccessor::Type::tSymlink; break;
        case DT_DIR: type = InputAccessor::Type::tDirectory; break;
        default: break; // unknown type
        }
        res.emplace(entry.name, type);
    }
    return res;
}

SourcePath SourcePath::resolveSymlinks(SymlinkResolution mode) const
{
    SourcePath res(CanonPath::root);

    int linksAllowed = 1024;

    std::list<std::string> todo;
    for (auto & c : path)
        todo.push_back(std::string(c));

    bool resolve_last = mode == SymlinkResolution::Full;

    while (!todo.empty()) {
        auto c = *todo.begin();
        todo.pop_front();
        if (c == "" || c == ".")
            ;
        else if (c == "..")
            res.path.pop();
        else {
            res.path.push(c);
            if (resolve_last || !todo.empty()) {
                if (auto st = res.maybeLstat(); st && st->type == InputAccessor::tSymlink) {
                    if (!linksAllowed--)
                        throw Error("infinite symlink recursion in path '%s'", path);
                    auto target = res.readLink();
                    res.path.pop();
                    if (target.starts_with("/"))
                        res.path = CanonPath::root;
                    todo.splice(todo.begin(), tokenizeString<std::list<std::string>>(target, "/"));
                }
            }
        }
    }

    return res;
}

}
