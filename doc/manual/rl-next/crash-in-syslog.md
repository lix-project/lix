---
synopsis: "Crashs land in syslog now"
cls: [2640]
category: Improvements
credits: [jade]
---

When Lix crashes with unexpected exceptions and in some other conditions, it prints bug reporting instructions.
Previously, these only landed in stderr and not in syslog.
However, on larger Lix installations, it may be the case that Lix crashes in the client without the logs landing in the system logs, which impeded diagnosis.

Now, such crashes always land in syslog too.
