#pragma once
/// @file Crash handler for Lix that prints back traces (hopefully in instances where it is not just going to crash the process itself).
/*
 * Author's note: This will probably be partially/fully supplanted by a
 * minidump writer like the following once we get our act together on crashes a
 * little bit more:
 * https://github.com/rust-minidump/minidump-writer
 * https://github.com/EmbarkStudios/crash-handling
 * (out of process implementation *should* be able to be done on-demand)
 *
 * Such an out-of-process implementation could then both make minidumps and
 * print stack traces for arbitrarily messed-up process states such that we can
 * safely give out backtraces for SIGSEGV and other deadly signals.
 */

namespace nix {

/** Registers the Lix crash handler for std::terminate (currently; will support more crashes later). See also detectStackOverflow().  */
void registerCrashHandler();

}
