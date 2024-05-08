{ runCommand, libcap }:
runCommand "cant-get-capabilities" { nativeBuildInputs = [ libcap.out ]; } ''
  if [ "$(/run/wrappers/bin/ohno 2>&1)" != "failed to inherit capabilities: Operation not permitted" ]; then
    echo "Oh no! We gained capabilities!"
    exit 1
  fi
  touch $out
''
