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
  shellScript = runCommand "check-syscalls.sh" { } ''
    mkdir -p $out/bin
    cp -L ${./check-syscalls.sh} $out/bin/check-syscalls.sh
    patchShebangs $out/bin/check-syscalls.sh
  '';
in
writeShellScriptBin "check-syscalls" ''
  ${shellScript}/bin/check-syscalls.sh ${syscalls-csv}
''
