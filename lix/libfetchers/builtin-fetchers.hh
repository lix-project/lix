#pragma once
///@file internal header for fetcher declarations

#include "lix/libfetchers/fetchers.hh"

namespace nix::fetchers {

std::unique_ptr<InputScheme> makeIndirectInputScheme();
std::unique_ptr<InputScheme> makePathInputScheme();
std::unique_ptr<InputScheme> makeFileInputScheme();
std::unique_ptr<InputScheme> makeTarballInputScheme();
std::unique_ptr<InputScheme> makeGitInputScheme();
std::unique_ptr<InputScheme> makeGitLockedInputScheme();
std::unique_ptr<InputScheme> makeMercurialInputScheme();
std::unique_ptr<InputScheme> makeGitHubInputScheme();
std::unique_ptr<InputScheme> makeGitLabInputScheme();
std::unique_ptr<InputScheme> makeSourceHutInputScheme();

}
