{ runCommand }:
runCommand "cant-get-root-in-sandbox" {} ''
  if /run/wrappers/bin/ohno; then
    echo "Oh no! We're root in the sandbox!"
    exit 1
  fi
  touch $out
''
