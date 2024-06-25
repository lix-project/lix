---
synopsis: "Lix no longer speaks the Nix remote-build worker protocol to clients or servers older than CppNix 2.3"
cls: [1207, 1208, 1206, 1205, 1204, 1203, 1479]
issues: [fj#325]
credits: [jade]
category: Breaking Changes
---

CppNix 2.3 was released in 2019, and is the new oldest supported version. We
will increase our support baseline in the future up to a final version of CppNix
2.18 (which may happen soon given that it is the only still-packaged and thus
still-tested >2.3 version), but this step already removes a significant amount
of dead, untested, code paths.

Lix speaks the same version of the protocol as CppNix 2.18 and that fact will
never change in the future; the Lix plans to replace the protocol for evolution
will entail a complete incompatible replacement that will be supported in
parallel with the old protocol. Lix will thus retain remote build compatibility
with CppNix as long as CppNix maintains protocol compatibility with 2.18, and
as long as Lix retains legacy protocol support (which will likely be a long
time given that we plan to convert it to a frozen-in-time shim).
