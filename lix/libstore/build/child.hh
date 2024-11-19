#pragma once
///@file

namespace nix {

/**
 * Common initialisation performed in child processes that are just going to
 * execve.
 *
 * These processes may not use ReceiveInterrupts as they do not have an
 * interrupt receiving thread.
 */
void commonExecveingChildInit();

}
