from logging import Logger
from pathlib import Path
from textwrap import dedent
from functional2.testlib.fixtures.nix import Nix
import re


def test_purity_traversal(nix: Nix, tmp_path: Path, logger: Logger):
    error_re = re.compile(r"error: access to absolute path '.+' is forbidden in pure eval mode")

    flake_dir = tmp_path / "flake"
    flake_dir.mkdir()

    evilpath = tmp_path / "sekrit.txt"
    evilpath.write_text("kitty kitty")

    evilnix = tmp_path / "default.nix"
    evilnix.write_text('"woof"')

    goodnix = flake_dir / "good.nix"
    goodnix.write_text("1")

    nested_dir = flake_dir / "nested" / "nested2"
    nested_dir.mkdir(parents=True)
    (nested_dir / "good.nix").write_text("1")

    (flake_dir / "evil-link").symlink_to(evilpath)
    (flake_dir / "evil-default-nix").symlink_to(tmp_path)
    (flake_dir / "less-evil-link").symlink_to(tmp_path / "link-to-flake")
    (tmp_path / "link-to-flake").symlink_to(flake_dir / "flake.nix")

    (flake_dir / "nested-good.nix").symlink_to("nested/nested2/good.nix")
    (flake_dir / "nested-bad.nix").symlink_to("nested/../../nested2/good.nix")

    (flake_dir / "flake.nix").write_text(
        dedent("""
        {
            inputs = {};
            outputs = inputs: {
                bad1 = "${@ABSPATH@}";
                bad2 = builtins.readFile "${@ABSPATH@}";
                bad3 = builtins.readFile @ABSPATH@;
                bad4 = builtins.readFile ./evil-link;
                bad5 = builtins.readFile "${./evil-link}";
                bad6 = builtins.readFile ./less-evil-link;
                bad7 = builtins.readFile "${./less-evil-link}";
                bad8 = import ./evil-default-nix;
                bad9 = import ./nested-bad.nix;

                good1 = builtins.readFile "${inputs.self.outPath}/good.nix";
                good2 = builtins.readFile ./good.nix;
                good3 = builtins.readFile "${./good.nix}";
                good4 = toString (import ./good.nix);
                good5 = toString (import ./nested-good.nix);
            };
        }
    """).replace("@ABSPATH@", str(evilpath.absolute()))
    )

    for idx in range(1, 10):
        cmd = nix.nix(["eval", f".#bad{idx}"], flake=True)
        cmd.cwd = flake_dir
        res = cmd.run().expect(1)
        logger.info(res.stderr_plain)
        assert error_re.search(res.stderr_plain)
    for idx in range(1, 6):
        cmd = nix.nix(["eval", f".#good{idx}"], flake=True)
        cmd.cwd = flake_dir
        res = cmd.run().expect(0)
        assert res.stdout_plain == '"1"'
