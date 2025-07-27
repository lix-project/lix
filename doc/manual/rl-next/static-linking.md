---
synopsis: "Lix libraries can now be linked statically"
issues: [fj#789]
cls: [3775, 3778]
category: Fixes
credits: [alois31]
---
Previously the pkg-config files distributed with Lix were only suitable for dynamic linkage, causing "undefined reference to…" linker errors when trying to link statically.
Private dependency information has now been added to make static linkage work as expected without user intervention.
In addition, relevant static libraries are now prelinked to avoid strange failures due to missing static initializers.
