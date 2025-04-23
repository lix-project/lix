#include <regex>

#include "lix/libutil/url-name.hh"
#include "regex.hh"

namespace nix {

static std::string const attributeNamePattern("[a-zA-Z0-9_-]+");
static std::regex const lastAttributeRegex = regex::parse("(?:" + attributeNamePattern + "\\.)*(?!default)(" + attributeNamePattern +")(\\^.*)?");
static std::string const pathSegmentPattern("[a-zA-Z0-9_-]+");
static std::regex const lastPathSegmentRegex = regex::parse(".*/(" + pathSegmentPattern +")");
static std::regex const secondPathSegmentRegex = regex::parse("(?:" + pathSegmentPattern + ")/(" + pathSegmentPattern +")(?:/.*)?");
static std::regex const gitProviderRegex = regex::parse("github|gitlab|sourcehut");
static std::regex const gitSchemeRegex = regex::parse("git($|\\+.*)");
static std::regex const defaultOutputRegex = regex::parse(".*\\.default($|\\^.*)");

std::optional<std::string> getNameFromURL(ParsedURL const & url)
{
    std::smatch match;

    /* If there is a dir= argument, use its value */
    if (url.query.count("dir") > 0) {
        return url.query.at("dir");
    }

    /* If the fragment isn't a "default" and contains two attribute elements, use the last one */
    if (std::regex_match(url.fragment, match, lastAttributeRegex)) {
        return match.str(1);
    }

    /* If this is a github/gitlab/sourcehut flake, use the repo name */
    if (
        std::regex_match(url.scheme, gitProviderRegex)
        && std::regex_match(url.path, match, secondPathSegmentRegex)
    ) {
        return match.str(1);
    }

    /* If it is a regular git flake, use the directory name */
    if (
        std::regex_match(url.scheme, gitSchemeRegex)
        && std::regex_match(url.path, match, lastPathSegmentRegex)
    ) {
        return match.str(1);
    }

    /* If everything failed but there is a non-default fragment, use it in full */
    if (!url.fragment.empty() && !std::regex_match(url.fragment, defaultOutputRegex))
        return url.fragment;

    /* If there is no fragment, take the last element of the path */
    if (std::regex_match(url.path, match, lastPathSegmentRegex))
        return match.str(1);

    /* If even that didn't work, the URL does not contain enough info to determine a useful name */
    return {};
}

}
