{
  lib,
  python3,
  writeShellScriptBin,
}:

writeShellScriptBin "build-release-notes" ''
  exec ${lib.getExe (python3.withPackages (p: [ p.python-frontmatter ]))} \
    ${./build-release-notes.py} "$@"
''
