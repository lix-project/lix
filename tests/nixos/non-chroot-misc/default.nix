{ ... }:
# Misc things we want to test inside of a non redirected, non chroot Nix store.
let
  nonAutoCleaningFailingDerivationCode = ''
    derivation {
      name = "scratch-failing";
      system = builtins.currentSystem;
      builder = "/bin/sh";
      args = [ (builtins.toFile "builder.sh" "echo bonjour > $out; echo out: $out; false") ];
    }
  '';
in
{
  name = "non-chroot-sandbox-misc";

  nodes.machine = {
  };

  testScript = { nodes }: ''
    import re
    start_all()

    # You might ask yourself why write such a convoluted thing?
    # The condition for fooling Nix into NOT cleaning up the output path are non trivial and unclear.
    # This is one of those: create a derivation, mkdir or touch the $out path, communicate it back.
    # Even with a sandboxed Lix, you will observe leftovers before 2.93.0. After this version, this test passes.
    result = machine.fail("""nix-build --substituters "" -E '${nonAutoCleaningFailingDerivationCode}' 2>&1""")
    match = re.search(r'out: (\S+)', result)
    assert match is not None, "Did not find Nix store path in the result of the failing build"
    outpath = match.group(1).strip()
    print(f"Found Nix store path: {outpath}")
    machine.fail(f'stat {outpath}')
  '';
}
