#include "lix/libutil/source-path.hh"
#include "file-system.hh"
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

static InputAccessor::Stat convertStat(const struct stat & st)
{
    return InputAccessor::Stat {
        .type =
            S_ISREG(st.st_mode) ? InputAccessor::tRegular :
            S_ISDIR(st.st_mode) ? InputAccessor::tDirectory :
            S_ISLNK(st.st_mode) ? InputAccessor::tSymlink :
            InputAccessor::tMisc,
        .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
    };
}

InputAccessor::Stat CheckedSourcePath::lstat() const
{
    return convertStat(nix::lstat(path.abs()));
}

std::optional<InputAccessor::Stat> CheckedSourcePath::maybeLstat() const
{
    if (auto st = nix::maybeLstat(path.abs())) {
        return convertStat(*st);
    } else {
        return std::nullopt;
    }
}

InputAccessor::Stat CheckedSourcePath::stat() const
{
    return convertStat(nix::stat(path.abs()));
}

std::optional<InputAccessor::Stat> CheckedSourcePath::maybeStat() const
{
    if (auto st = nix::maybeStat(path.abs())) {
        return convertStat(*st);
    } else {
        return std::nullopt;
    }
}

InputAccessor::DirEntries CheckedSourcePath::readDirectory() const
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

}
