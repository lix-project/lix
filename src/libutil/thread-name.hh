#pragma once
///@file

namespace nix {

/**
 * Sets the name of the current operating system thread for the benefit of
 * debuggers.
 */
void setCurrentThreadName(const char * name);

}
