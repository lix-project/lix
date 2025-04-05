#pragma once
///@file

#include "lix/libutil/error.hh"

namespace nix {

/**
 * Exit the program with a given exit code.
 */
class Exit : public BaseException
{
public:
    int status;
    Exit() : status(0) { }
    explicit Exit(int status) : status(status) { }
};

}
