"""
Tests that:
- flake.nix may reside inside of a git submodule
- the flake can access content outside of the submodule

  rootRepo
  ├── root.nix
  └── submodule
      ├── flake.nix
      └── sub.nix
"""

from testlib.fixtures.nix import Nix
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File, EnvTemplate
from testlib.utils import get_global_asset_pack
from pathlib import Path
import pytest
import re
import json
import shutil

root_without_self = get_global_asset_pack(".git") | {"root.nix": File('"expression in root repo"')}

root_with_self = root_without_self | {
    "flake.nix": File("""{
      inputs.self.submodules = true;
      outputs = { self }: {
        foo = self.outPath;
      };
    }""")
}

submodule = get_global_asset_pack(".git") | {
    "flake.nix": File("""{
        outputs = { self }: {
            sub = import ./sub.nix;
            root = import ../root.nix;
        };
    }"""),
    "sub.nix": File('"expression in submodule"'),
}


@pytest.fixture(autouse=True)
def common_init(nix: Nix, env: ManagedEnv, git: Git, files: Path):
    nix.settings.add_xp_feature("nix-command", "flakes")

    # Submodules can't be fetched locally by default, which can cause
    # information leakage vulnerabilities, but for these tests our
    # submodule is intentionally local and it's all trusted, so we
    # disable this restriction. Setting it per repo is not sufficient, as
    # the repo-local config does not apply to the commands run from
    # outside the repos by Nix.
    env["XDG_CONFIG_HOME"] = str(env.dirs.home / ".config")
    git(None, "config", "--global", "protocol.file.allow", "always")

    git(files / "submodule", "add", "flake.nix", "sub.nix")
    git(files / "submodule", "commit", "-m", "Initial")

    git(files / "rootRepo", "submodule", "init")
    git(files / "rootRepo", "submodule", "add", files / "submodule", "submodule")
    git(files / "rootRepo", "add", "root.nix")
    git(files / "rootRepo", "commit", "-m", "Add root.nix")


@with_files({"rootRepo": root_without_self, "submodule": submodule})
class TestSubmoduleAccess:
    @pytest.fixture(autouse=True)
    def init(self, files: Path):
        self.flakeref = f"git+file://{files}/rootRepo?submodules=1&dir=submodule"

    def test_flake_access_via_dir_param(self, nix: Nix):
        result = nix.nix(["eval", "--json", f"{self.flakeref}#sub"]).run().json()
        assert result == "expression in submodule"

    def test_flake_access_parents_via_dir_param(self, nix: Nix):
        result = nix.nix(["eval", "--json", f"{self.flakeref}#root"]).run().json()
        assert result == "expression in root repo"


@with_files(
    {
        "rootRepo": root_with_self,
        "submodule": submodule,
        "otherRepo": get_global_asset_pack(".git")
        | {
            "flake.nix": EnvTemplate("""{
          inputs.root.url = "git+file://@HOME@/rootRepo";
          outputs = { self, root }: {
            foo = root.foo;
          };
        }""")
        },
    }
)
class TestSelf:
    @pytest.fixture(autouse=True)
    def init(self, git: Git, files: Path):
        git(files / "rootRepo", "add", "flake.nix")
        git(files / "rootRepo", "commit", "-m", "Bla")

    def test_no_access_without_xp_feature(self, nix: Nix, files: Path):
        result = nix.nix(["eval", "--raw", f"{files}/rootRepo#foo"]).run().expect(1)
        assert "experimental Lix feature 'flake-self-attrs' is disabled" in result.stderr_s

    def test_access_with_xp_feature(self, nix: Nix, files: Path):
        nix.settings.add_xp_feature("flake-self-attrs")

        result = nix.nix(["eval", "--raw", f"{files}/rootRepo#foo"]).run().ok()
        assert (Path(result.stdout_s.strip()) / "submodule").exists()

    class TestSelfDependency:
        @pytest.fixture(autouse=True)
        def init_dep(self, git: Git, files: Path):
            git(files / "otherRepo", "add", "flake.nix")

        def test_no_access_without_xp_feature(self, nix: Nix, files: Path):
            result = nix.nix(["eval", "--raw", f"{files}/otherRepo#foo"]).run().expect(1)
            assert "experimental Lix feature 'flake-self-attrs' is disabled" in result.stderr_s

        def test_access_with_xp_feature(self, nix: Nix, files: Path):
            nix.settings.add_xp_feature("flake-self-attrs")

            result = nix.nix(["eval", "--raw", "-vvvvv", f"{files}/otherRepo#foo"]).run().ok()
            assert re.search(
                r"refetching input 'git\+file://.+/rootRepo.+' due to self attribute",
                result.stderr_s,
            )
            assert (Path(result.stdout_s.strip()) / "submodule").exists()

        def test_submodules_are_locked(self, nix: Nix, files: Path):
            nix.settings.add_xp_feature("flake-self-attrs")

            nix.nix(["eval", "--raw", "-vvvvv", f"{files}/otherRepo#foo"]).run().ok()
            lockfile = json.loads((files / "otherRepo/flake.lock").read_text())
            assert lockfile["nodes"]["root_2"]["locked"]["submodules"]

        def test_double_eval_does_not_refetch(self, nix: Nix, files: Path):
            nix.settings.add_xp_feature("flake-self-attrs")

            nix.nix(["eval", "--raw", f"{files}/otherRepo#foo"]).run().ok()

            shutil.rmtree(nix.env.dirs.home / ".cache")
            nix.clear_store()

            result = nix.nix(["eval", "--raw", "-vvvvv", f"{files}/otherRepo#foo"]).run().ok()
            assert "refetching" not in result.stderr_s
