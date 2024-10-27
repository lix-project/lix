source ../common.sh

clearStore

evil=$(cat <<-'EOF'
builtins.derivation {
  name = "evil-kbity";
  system = "x86_64-darwin";
  builder = "/bin/sh";
  args = [ "-c" "> $out" ];
  __structuredAttrs = true;
  env.oops = "lol %s";
}
EOF
)

# This should not crash
nix-shell --expr "$evil" --run 'echo yay' | grepQuiet yay
