import re
import json
import pytest
from textwrap import dedent

from testlib.fixtures.file_helper import File, with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset, get_global_asset_pack
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.command import Command
from testlib.environ import environ

system = environ.get("system")

flake = {
    "flake.nix": File("""
        {
          description = "Bla bla";

          outputs = { self }: with import ./config.nix; rec {
            packages.${system}.default = mkDerivation {
              name = "profile-test-${builtins.readFile ./version}";
              outputs = [ "out" "man" "dev" ];
              builder = builtins.toFile "builder.sh"
                ''
                  mkdir -p $out/bin
                  cat > $out/bin/hello <<EOF
                  #! ${shell}
                  echo Hello ${builtins.readFile ./who}
                  EOF
                  chmod +x $out/bin/hello
                  echo DONE
                  mkdir -p $man/share/man
                  mkdir -p $dev/include
                '';
              __contentAddressed = import ./ca.nix;
              outputHashMode = "recursive";
              outputHashAlgo = "sha256";
              meta.outputsToInstall = [ "out" "man" ];
            };
          };
        }
    """),
    "who": File("World"),
    "version": File("1.0"),
    "ca.nix": File("false"),
    "config.nix": get_global_asset("config.nix"),
}


@pytest.fixture(autouse=True)
def configure_system(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


def run_hello(env: ManagedEnv) -> str:
    return Command([str(env.dirs.home / ".nix-profile/bin/hello")], _env=env).run().ok().stdout_s


def build(nix: Nix, thing: str) -> str:
    return nix.nix(["build", "--no-link", "--print-out-paths", thing]).run().ok().stdout_s.strip()


class TestUpgradeProfile:
    @with_files({"flake1": flake} | get_global_asset_pack("user-envs"))
    def test_nix_env_upgrade(self, nix: Nix, env: ManagedEnv):
        nix.nix_env(["-f", "./user-envs.nix", "-i", "foo-1.0"]).run().ok()
        result = nix.nix(["profile", "list"]).run().ok().stdout_s
        assert re.match(r"Name:.*foo\nStore paths:.*foo-1.0", result)

        nix.nix(["profile", "install", "./flake1", "-L"]).run().ok()
        result = nix.nix(["profile", "list"]).run().ok().stdout_s
        assert re.search(r"Name:.*flake1\n.*?\n.*?\nLocked flake URL:.*narHash", result)

        assert run_hello(env) == "Hello World\n"
        assert (env.dirs.home / ".nix-profile/share/man").exists()
        assert not (env.dirs.home / ".nix-profile/include").exists()

        result = nix.nix(["profile", "history"]).run().ok().stdout_s
        assert f"packages.{system}.default: ∅ -> 1.0" in result

        result = nix.nix(["profile", "diff-closures"]).run().ok().stdout_s
        assert "env-manifest.nix: ε → ∅" in result

    @with_files({"flake1": flake})
    def test_upgrade_from_v2(self, nix: Nix, env: ManagedEnv):
        import_profile = env.dirs.home / "import-profile"
        import_profile.mkdir()

        path = build(nix, "./flake1^out")
        (import_profile / "manifest.json").write_text(
            json.dumps(
                {
                    "version": 2,
                    "elements": [
                        {
                            "active": True,
                            "attrPath": "legacyPackages.x86_64-linux.hello",
                            "originalUrl": "flake:nixpkgs",
                            "outputs": None,
                            "priority": 5,
                            "storePaths": [path],
                            "url": "github:NixOS/nixpkgs/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        }
                    ],
                }
            )
        )

        added_path = nix.nix(["store", "add-path", str(import_profile)]).run().ok().stdout_s.strip()
        nix.nix(
            ["build", "--profile", str(env.dirs.home / ".nix-profile"), str(added_path)]
        ).run().ok()

        result = nix.nix(["profile", "list"]).run().ok().stdout_s
        assert re.match("Name:.*hello\n.*\nStore paths:.*" + path, result, re.S)
        assert (
            "removed 1 packages, kept 0 packages"
            in nix.nix(["profile", "remove", "hello"]).run().ok().stderr_s
        )


class ProfileTestBase:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix, configure_system, files):  # noqa: ANN001, ARG002
        nix.nix(["profile", "install", "./flake1", "-L"]).run().ok()


class TestBasicOperations(ProfileTestBase):
    @with_files({"flake1": flake})
    def test_xdg_base_dir_support(self, nix: Nix, env: ManagedEnv):
        nix.settings.use_xdg_base_directories = True

        result = nix.nix(["profile", "remove", "flake1", "-L"]).run().ok()
        assert "removed 1 packages" in result.stderr_s

        nix.nix(["profile", "install", "./flake1", "-L"]).run().ok()
        assert run_hello(env) == "Hello World\n"

    @with_files({"flake1": flake})
    def test_wipe_history(self, nix: Nix):
        nix.nix(["profile", "wipe-history"]).run().ok()
        history = nix.nix(["profile", "history"]).run().ok().stdout_s
        assert len(re.findall(r"^Version ", history, re.M)) == 1

    @with_files({"flake1": flake} | get_global_asset_pack("simple-drv"))
    def test_install_non_flake(self, nix: Nix, env: ManagedEnv):
        nix.nix(["profile", "install", "--file", "./simple.nix", ""]).run().ok()
        assert (env.dirs.home / ".nix-profile/hello").read_text() == "Hello World!\n"
        assert "removed 1 packages" in nix.nix(["profile", "remove", "simple"]).run().ok().stderr_s

        assert not (env.dirs.home / ".nix-profile/hello").exists()
        output = nix.nix_build(["--no-out-link", "./simple.nix"]).run().ok().stdout_s.strip()
        nix.nix(["profile", "install", output]).run().ok()
        assert (env.dirs.home / ".nix-profile/hello").read_text() == "Hello World!\n"

    @with_files(
        {
            "flake1": flake,
            "simple1": get_global_asset_pack("simple-drv"),
            "simple2": get_global_asset_pack("simple-drv"),
        }
    )
    def test_install_different_sources(self, nix: Nix):
        nix.nix(["profile", "install", "--file", "./simple1/simple.nix", ""]).run().ok()
        nix.nix(["profile", "install", "--file", "./simple2/simple.nix", ""]).run().ok()

        result = nix.nix(["profile", "list"]).run().ok().stdout_s
        assert re.search(r"Name:.*simple", result)
        assert re.search(r"Name:.*simple-1", result)

        assert "removed 1 packages" in nix.nix(["profile", "remove", "simple"]).run().ok().stderr_s
        assert (
            "removed 1 packages" in nix.nix(["profile", "remove", "simple-1"]).run().ok().stderr_s
        )

    @with_files({"flake1": flake, "flake2": flake | {"who": File("World2")}})
    def test_priority(self, nix: Nix, env: ManagedEnv):
        hello1 = build(nix, "./flake1#default.out")
        hello2 = build(nix, "./flake2#default.out")

        result = nix.nix(["--offline", "profile", "install", "./flake2"]).run().expect(1)
        result = [
            line
            for line in result.stderr_s.splitlines()
            if not line.startswith("warning: ")
            and not line.startswith("error (ignored): ")
            and not re.match(r"^fetching .+ input", line)
            and line
        ]
        assert result == [
            "error: An existing package already provides the following file:",
            f"         {hello1}/bin/hello",
            "       This is the conflicting file from the new package:",
            f"         {hello2}/bin/hello",
            "       To remove the existing package:",
            f"         nix profile remove path:{env.dirs.home}/flake1#packages.{system}.default",
            "       The new package can also be installed next to the existing one by assigning a different priority.",
            "       The conflicting packages have a priority of 5.",
            "       To prioritise the new package:",
            f"         nix profile install path:{env.dirs.home}/flake2#packages.{system}.default --priority 4",
            "       To prioritise the existing package:",
            f"         nix profile install path:{env.dirs.home}/flake2#packages.{system}.default --priority 6",
        ]

        assert run_hello(env) == "Hello World\n"
        nix.nix(["profile", "install", "./flake2", "--priority", "100"]).run().ok()
        assert run_hello(env) == "Hello World\n"
        nix.nix(["profile", "install", "./flake2", "--priority", "0"]).run().ok()
        assert run_hello(env) == "Hello World2\n"
        nix.nix(["profile", "install", "./flake1", "--priority", "100"]).run().ok()
        assert run_hello(env) == "Hello World2\n"


@with_files({"flake1": flake})
class TestProfileContentUpgrade(ProfileTestBase):
    @pytest.fixture(autouse=True)
    def upgrade(self, nix: Nix, env: ManagedEnv, setup):  # noqa: ANN001, ARG002
        (env.dirs.home / "flake1/who").write_text("NixOS")
        (env.dirs.home / "flake1/version").write_text("2.0")

        nix.nix(["profile", "upgrade", "flake1"]).run().ok()

    def test_upgrade_succeeded(self, nix: Nix, env: ManagedEnv):
        assert run_hello(env) == "Hello NixOS\n"

        result = nix.nix(["profile", "history"]).run().ok().stdout_s
        assert f"packages.{system}.default: 1.0, 1.0-man -> 2.0, 2.0-man" in result

    def test_diff_closures_works(self, nix: Nix):
        assert nix.nix(["profile", "diff-closures"]).run().stdout_s == dedent("""\
            Version 1 -> 2:
              profile-test: 1.0 → 2.0
        """)

    def test_rollback_works(self, nix: Nix, env: ManagedEnv):
        nix.nix(["profile", "rollback"]).run().ok()
        assert run_hello(env) == "Hello World\n"


@with_files({"flake1": flake} | get_global_asset_pack("user-envs"))
def test_uninstall(nix: Nix, env: ManagedEnv):
    nix.nix_env(["-f", "./user-envs.nix", "-i", "foo-1.0"]).run().ok()
    nix.nix(["profile", "install", "./flake1", "-L"]).run().ok()

    assert (env.dirs.home / ".nix-profile/bin/foo").exists()
    assert "removed 1 packages" in nix.nix(["profile", "remove", "foo"]).run().ok().stderr_s
    assert not (env.dirs.home / ".nix-profile/bin/foo").exists()
    assert "foo: 1.0 -> ∅" in nix.nix(["profile", "history"]).run().ok().stdout_s
    assert "Version 1 -> 2" in nix.nix(["profile", "diff-closures"]).run().ok().stdout_s


@with_files({"flake1": flake})
def test_output_override(nix: Nix, env: ManagedEnv):
    profile = env.dirs.home / ".nix-profile"

    nix.nix(["profile", "install", "./flake1^*"]).run().ok()
    assert run_hello(env) == "Hello World\n"
    assert (profile / "share/man").exists()
    assert (profile / "include").exists()

    assert (env.dirs.home / "flake1/who").write_text("Lix")
    nix.nix(["profile", "upgrade", "flake1"]).run().ok()
    assert run_hello(env) == "Hello Lix\n"
    assert (profile / "share/man").exists()
    assert (profile / "include").exists()

    assert "removed 1 packages" in nix.nix(["profile", "remove", "flake1"]).run().ok().stderr_s
    nix.nix(["profile", "install", "./flake1^man"]).run().ok()
    assert not (profile / "bin/hello").exists()
    assert (profile / "share/man").exists()
    assert not (profile / "include").exists()


@with_files({"flake1": flake, "flake2": flake})
def test_conflict_resolution_cppnix_8284(nix: Nix):
    path = build(nix, "./flake1^out")
    nix.nix(["profile", "install", path]).run().ok()

    expr = f'(builtins.getFlake "./flake2").packages.{system}.default'
    nix.nix(["profile", "install", "--impure", "--expr", expr]).run().expect(1)
