from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File, EnvTemplate, CopyFile
from testlib.environ import environ
from testlib.utils import get_global_asset, get_global_asset_pack
from pathlib import Path
import pytest

system = environ.get("system")

flake = {
    "flake.nix": EnvTemplate(f"""{{
        inputs.nixpkgs.url = "@HOME@/nixpkgs";
        outputs = {{self, nixpkgs}}: {{
          packages.{system}.hello = (import ./config.nix).mkDerivation {{
            name = "hello";
            outputs = [ "out" "dev" ];
            meta.outputsToInstall = [ "out" ];
            buildCommand = "";
          }};
        }};
    }}"""),
    "config.nix": get_global_asset("config.nix"),
    "shell-hello.nix": CopyFile("assets/shell-hello.nix"),
}
nixpkgs = {
    "flake.nix": File(f"""{{
        outputs = {{self}}: {{
          legacyPackages.{system}.bashInteractive = (import ./shell.nix {{}}).bashInteractive;
        }};
    }}"""),
    "config.nix": get_global_asset("config.nix"),
    "shell.nix": get_global_asset("shell.nix"),
}


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


@with_files(flake | {"nixpkgs": nixpkgs})
class TestDevelop:
    def test_env_passthrough(self, nix: Nix):
        nix.env["ENVVAR"] = "a"

        result = (
            nix.nix(["develop", "--no-write-lock-file", ".#hello"])
            .with_stdin(b'echo "$ENVVAR"')
            .run()
            .ok()
        )
        assert result.stdout_s == "a\n"

    def test_no_env_passthrough_on_ignore_environment(self, nix: Nix):
        nix.env["ENVVAR"] = "a"

        result = (
            nix.nix(["develop", "--ignore-environment", "--no-write-lock-file", ".#hello"])
            .with_stdin(b'echo "$ENVVAR"')
            .run()
            .ok()
        )
        assert result.stdout_s == "\n"

    class TestShell:
        @pytest.fixture(autouse=True)
        def shell_init(self, nix: Nix, files: Path):  # noqa: ARG002
            nix.nix(
                [
                    "build",
                    "--no-write-lock-file",
                    "./nixpkgs#bashInteractive",
                    "--out-link",
                    "./bash-interactive",
                ]
            ).run().ok()
            self.bash_interactive = nix.env.dirs.home / "bash-interactive/bin/bash"

            nix.env["SHELL"] = "custom"

        @pytest.mark.parametrize("extra_args", [[], ["--ignore-environment"]])
        def test_shell_is_bash_interactive(self, nix: Nix, extra_args: list[str]):
            result = (
                nix.nix(["develop", *extra_args, "--no-write-lock-file", ".#hello"])
                .with_stdin(b'echo "$SHELL"')
                .run()
                .ok()
            )
            assert Path(result.stdout_s.strip()).samefile(self.bash_interactive)

    @pytest.mark.parametrize("extra_args", [[], ["--ignore-environment"]])
    def test_sets_in_nix_shell(self, nix: Nix, extra_args: list[str]):
        result = (
            nix.nix(
                [
                    "develop",
                    "--no-write-lock-file",
                    *extra_args,
                    ".#hello",
                    "-c",
                    "sh",
                    "-c",
                    "echo $IN_NIX_SHELL",
                ]
            )
            .run()
            .ok()
        )
        assert result.stdout_s == "impure\n"


@with_files(
    {
        "t": flake | get_global_asset_pack(".git") | {".gitignore": File("flake.lock\n")},
        "nixpkgs": nixpkgs,
    }
)
def test_ignoring_lockfile_does_not_break_develop(nix: Nix, files: Path, git: Git):
    git(files / "t", "add", "config.nix", "shell-hello.nix", "flake.nix", ".gitignore")

    nix.nix(["develop", ".#hello"], cwd=files / "t").with_stdin(b"true").run().ok()
