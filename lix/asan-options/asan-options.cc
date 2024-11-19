/// @file This is very bothersome code that has to be included in every
/// executable to get the correct default ASan options. I am so sorry.

extern "C" [[gnu::retain]] const char *__asan_default_options()
{
    // We leak a bunch of memory knowingly on purpose. It's not worthwhile to
    // diagnose that memory being leaked for now.
    //
    // Instruction bytes are useful for finding the actual code that
    // corresponds to an ASan report.
    //
    // TODO: setting log_path=asan.log or not: neither works, since you can't
    // write to the fs in certain places in the testsuite, but you also cannot
    // write arbitrarily to stderr in other places so the reports get eaten.
    // pain 🥖
    return "halt_on_error=1:abort_on_error=1:detect_leaks=0:print_summary=1:dump_instruction_bytes=1";
}
