{
  runCommand,
  lib,
  libseccomp,
  writeShellScriptBin,
}:
let
  syscalls-csv = runCommand "syscalls.csv" { } ''
    echo ${lib.escapeShellArg libseccomp.src}
    tar -xf ${lib.escapeShellArg libseccomp.src} --strip-components=2 ${libseccomp.name}/src/syscalls.csv
    mv syscalls.csv "$out"
  '';
in
writeShellScriptBin "check-syscalls" ''
  ${./check-syscalls.sh} ${syscalls-csv}
''
