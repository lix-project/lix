---
name: cr-line-endings
internalName: CRLineEndings
timeline:
  - date: 2025-02-05
    release: 2.93.0
    cls: [2475]
    message: Introduced as a parser error.
---
CR (`\r`) and CRLF (`\r\n`) are deprecated as line delimiters in Nix code.
The reason for the CR line ending deprecation is that no OS still uses them anymore.
The reason for the CRLF line ending deprecation is that there is a fatal bug in the indentation-stripping logic of indented strings,
breaking all indented strings when CRLF indentation is used.
In a future language revision, the CRLF line endings will be allowed again but with fixed semantics.

To fix this, convert all files to have regular `\n` endings and disable all software which performs platform-specific normalizations.
