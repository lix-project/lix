---
synopsis: Lix turns more internal bugs into crashes
cls: [797, 626]
---

Lix now enables build options such as trapping on signed overflow and enabling
libstdc++ assertions by default. These may find new bugs in Lix, which will
present themselves as Lix processes aborting, potentially without an error
message.

If Lix processes abort on your machine, this is a bug. Please file a bug,
ideally with the core dump (or information from it).

On Linux, run `coredumpctl list`, find the crashed process's PID at
the bottom of the list, then run `coredumpctl info THE-PID`. You can then paste
the output into a bug report.

On macOS, open the Console app from Applications/Utilities, select Crash
Reports, select the crash report in question. Right click on it, select Open In
Finder, then include that file in your bug report. [See the Apple
documentation][apple-crashreport] for more details.

[apple-crashreport]: https://developer.apple.com/documentation/xcode/acquiring-crash-reports-and-diagnostic-logs#Locate-crash-reports-and-memory-logs-on-the-device
