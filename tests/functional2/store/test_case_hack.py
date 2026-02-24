from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import CopyFile, with_files

files = {"case.nar": CopyFile("assets/case.nar")}

opts = ["--option", "use-case-hack", "true"]

expected = {
    "x",
    "x/FOO",
    "x/Foo~nix~case~hack~1",
    "x/foo~nix~case~hack~2",
    "x/foo~nix~case~hack~2/A",
    "x/foo~nix~case~hack~2/A/foo",
    "x/foo~nix~case~hack~2/a~nix~case~hack~1",
    "x/foo~nix~case~hack~2/a~nix~case~hack~1/foo",
    "x/foo-1",
    "x/foo0",
    "xt_CONNMARK.h",
    "xt_CONNmark.h~nix~case~hack~1",
    "xt_connmark.h~nix~case~hack~2",
}


@with_files(files)
def test_case_hack(nix: Nix):
    nar_path = nix.env.dirs.home / "case.nar"
    nar = nar_path.read_bytes()
    out = nix.env.dirs.home / "case"

    # Check whether restoring and dumping a NAR that contains case
    # collisions is round-tripping, even on a case-insensitive system.
    nix.nix_store([*opts, "--restore", out]).with_stdin(nar).run().ok()
    assert set({str(p.relative_to(out)) for p in out.rglob("*")}) == expected

    dumped = nix.nix_store([*opts, "--dump", out]).run().ok().stdout
    assert dumped == nar

    out_hash = nix.nix([*opts, "--type", "sha256", out], nix_exe="nix-hash").run().ok().stdout_plain
    nar_hash = (
        nix.nix(["--flat", "--type", "sha256", nar_path], nix_exe="nix-hash")
        .run()
        .ok()
        .stdout_plain
    )
    assert out_hash == nar_hash

    # Check whether we detect true collisions (e.g. those remaining after
    # removal of the suffix).
    (out / "xt_CONNMARK.h~nix~case~hack~3").write_text("")
    assert "file name collision" in nix.nix_store([*opts, "--dump", out]).run().expect(1).stderr_s
