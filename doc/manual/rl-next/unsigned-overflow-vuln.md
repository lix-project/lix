---
synopsis: "Fix unsigned overflow leading to out-of-band write in the NAR parser"
cls: [5550]
category: "Fixes"
credits: [horrors, raito, edef, sandydoo]
issues: []
---

The NAR parser contained an unsigned integer overflow that could be used by an
attacker to write arbitrary data to an unknown memory location and possibly
achieve code execution. A successful attack on the system-wide Lix daemon
could lead to privilege escalation to root. Any process that involves NAR
serialization could trigger this issue, including (but not limited to)

  - local user interaction, whether the users are trusted or untrusted
  - malicious substituters sending malformed NARs
  - remote builders sending malformed build results
  - remote daemons sending malformed inputs when requesting remote builds

Successful attacks using this bug require ASLR weakening of some sort, whether
by architecture constraints (e.g. on 32 bit systems, where little randomization
is possible) or system configuration (e.g. low ASLR entropy when loading
libraries), and millions of attempts. Local attacks can be mounted in less than
an hour. Remote builds typically require a fresh SSH connection for each build
and are thus less susceptible. Only one attempt can be made by substituters for
every build using substituters, they are thus not a likely vector for attacks.

At the time of writing, MITRE has not assigned this a CVE yet.
