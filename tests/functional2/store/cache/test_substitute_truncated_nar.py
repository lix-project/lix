from textwrap import dedent

from testlib.fixtures.nix import Nix


def test_substitute_truncated_nar(nix: Nix):
    drv = dedent("""
        derivation {
            name = "text";
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "echo some text to make the nar less empty > $out" ];
        }
    """)
    cache_dir = nix.env.dirs.cache_dir
    cache_uri = f"file://{cache_dir}?compression=none"
    nar = cache_dir / "nar" / "0513ia03lmqyq8bipmvv0awjji48li22rbmm9p5iwzm08y8m810z.nar"
    nix.settings.feature("nix-command")

    res = nix.nix_build(["--no-out-link", "--expr", drv]).run().ok()
    out_path = res.stdout_plain

    res = nix.nix(["copy", "--to", cache_uri, out_path]).run().ok()
    assert "copying 1 path" in res.stderr_plain

    assert nar.exists()

    res = nix.nix([], "nix-collect-garbage", build=True).run().ok()
    # .drv file + built path
    assert "2 store paths deleted" in res.stderr_plain

    args = [
        "--no-out-link",
        "--debug",
        "--option",
        "substituters",
        cache_uri,
        "--option",
        "require-sigs",
        "false",
        "-j0",
        "--expr",
        drv,
    ]

    # Ensure we can substitute with a non-corrupted nar
    res = nix.nix_build(args).run().ok()

    nar.write_bytes(nar.read_bytes()[:-10])
    nix.clear_store()

    res = nix.nix_build(args).run().expect(1)
    assert "error: some substitutes for the outputs of derivation" in res.stderr_plain
    assert "error: bad archive: unexpected end of nar encountered" in res.stderr_plain
