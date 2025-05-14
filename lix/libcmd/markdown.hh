#pragma once
///@file

#include "lix/libutil/terminal.hh"
#include "lix/libutil/types.hh"

namespace nix {

std::string renderMarkdownToTerminal(std::string_view markdown, StandardOutputStream fileno = StandardOutputStream::Stdout);

}
