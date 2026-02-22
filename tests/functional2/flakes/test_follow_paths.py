from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File, EnvTemplate
from testlib.utils import get_global_asset_pack
from pathlib import Path
import pytest
import fnmatch
import json


@pytest.fixture(autouse=True)
def add_xp_features(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


@with_files(
    {
        "flakeA": get_global_asset_pack(".git")
        | {
            "flake.nix": EnvTemplate("""{
                inputs = {
                    B = {
                        url = "path:./flakeB";
                        inputs.foobar.follows = "foobar";
                    };
                    foobar.url = "path:@HOME@/flakeA/flakeE";
                };
                outputs = { ... }: {};
            }"""),
            "flakeB": {
                "flake.nix": EnvTemplate("""{
                    inputs = {
                        foobar.url = "path:@HOME@/flakeA/flakeE";
                        goodoo.follows = "C/goodoo";
                        C = {
                            url = "path:./flakeC";
                            inputs.foobar.follows = "foobar";
                        };
                    };
                    outputs = { ... }: {};
                }"""),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                        inputs = {
                            foobar.url = "path:@HOME@/flakeA/flakeE";
                            goodoo.follows = "foobar";
                        };
                        outputs = { ... }: {};
                    }""")
                },
            },
            "flakeD": {"flake.nix": File("{ outputs = { ... }: {}; }")},
            "flakeE": {"flake.nix": File("{ outputs = { ... }: {}; }")},
        }
    }
)
class TestPathFollows:
    @pytest.fixture(autouse=True)
    def init(self, git: Git, files: Path):
        git(files / "flakeA", "add", ".")

    def test_metadata(self, nix: Nix, files: Path):
        out = nix.nix(["flake", "metadata", files / "flakeA"]).run().ok()
        assert "B: path" in out.stdout_s
        assert "C: path" in out.stdout_s
        assert "foobar: path" in out.stdout_s
        assert "goodoo follows" in out.stdout_s

    def test_update(self, nix: Nix, files: Path):
        out = nix.nix(["flake", "update", "--flake", files / "flakeA"]).run().ok()
        assert "Added input 'B" in out.stderr_s
        assert "Added input 'B/C" in out.stderr_s
        assert "Added input 'B/C/foobar" in out.stderr_s
        assert "Added input 'B/C/goodoo" in out.stderr_s
        assert "Added input 'B/foobar" in out.stderr_s
        assert "Added input 'B/goodoo" in out.stderr_s

    def test_lock(self, nix: Nix, files: Path):
        nix.nix(["flake", "lock", files / "flakeA"]).run().ok()
        lockfile = json.loads((files / "flakeA/flake.lock").read_text())

        assert lockfile["nodes"]["B"]["inputs"]["C"] == "C"
        assert lockfile["nodes"]["B"]["inputs"]["foobar"] == ["foobar"]
        assert lockfile["nodes"]["C"]["inputs"]["foobar"] == ["B", "foobar"]

        # Ensure that locking twice doesn't change anything
        nix.nix(["flake", "lock", files / "flakeA"]).run().ok()
        lockfile2 = json.loads((files / "flakeA/flake.lock").read_text())
        assert lockfile == lockfile2

    def test_removing_follows_updates_lockfile(self, nix: Nix, files: Path, git: Git):
        git(files / "flakeA", "add", ".")

        nix.nix(["flake", "lock", files / "flakeA"]).run().ok()
        (files / "flakeA/flake.nix").write_text("""{
            inputs = {
                B.url = "path:./flakeB";
                D.url = "path:./flakeD";
            };
            outputs = { ... }: {};
        }""")

        nix.nix(["flake", "lock", files / "flakeA"]).run().ok()

        lockfile = json.loads((files / "flakeA/flake.lock").read_text())
        assert lockfile["nodes"]["B"]["inputs"]["foobar"] == "foobar"
        assert "foobar" in lockfile["nodes"]


@with_files(
    {
        "flakeA": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
                inputs.B.url = "path:../flakeB";
                outputs = { ... }: {};
            }""")
        }
    }
)
def test_relative_path_cant_leave_store_path(nix: Nix, files: Path, git: Git):
    git(files / "flakeA", "add", ".")

    out = nix.nix(["flake", "lock", files / "flakeA"]).run().expect(1)
    assert "points outside" in out.stderr_s


@with_files(
    {
        "flakeA": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
                inputs.B = {
                    url = "path:./flakeB";
                    inputs.invalid.follows = "D";
                    inputs.invalid2.url = "path:./flakeD";
                };
                inputs.D.url = "path:./flakeD";
                outputs = { ... }: {};
            }"""),
            "flakeB": {
                "flake.nix": EnvTemplate("""{
                    inputs = {
                        foobar.url = "path:@HOME@/flakeA/flakeE";
                        goodoo.follows = "C/goodoo";
                        C = {
                            url = "path:./flakeC";
                            inputs.foobar.follows = "foobar";
                        };
                    };
                    outputs = { ... }: {};
                }"""),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                        inputs = {
                            foobar.url = "path:@HOME@/flakeA/flakeE";
                            goodoo.follows = "foobar";
                        };
                        outputs = { ... }: {};
                    }""")
                },
            },
            "flakeD": {"flake.nix": File("{ outputs = { ... }: {}; }")},
            "flakeE": {"flake.nix": File("{ outputs = { ... }: {}; }")},
        }
    }
)
def test_nonexistent_follows_print_warnings(nix: Nix, files: Path, git: Git):
    git(files / "flakeA", "add", ".")

    out = nix.nix(["flake", "lock", files / "flakeA"]).run().ok()
    assert "warning: input 'B' has an override for a non-existent input 'invalid'" in out.stderr_s
    assert "warning: input 'B' has an override for a non-existent input 'invalid2'" in out.stderr_s


@with_files(
    {
        "flakeA": get_global_asset_pack(".git")
        | {
            # input B/D should be able to be found...
            "flake.nix": File("""{
                inputs = {
                    B = {
                        url = "path:./flakeB";
                        inputs.C.follows = "C";
                    };
                    C.url = "path:./flakeB/flakeC";
                };
                outputs = { ... }: {};
            }"""),
            "flakeB": {
                "flake.nix": File("""{
                    inputs = {
                        C.url = "path:./flakeC";
                        D.follows = "C/D";
                    };
                    outputs = { ... }: {};
                }"""),
                "flakeC": {
                    "flake.nix": File("""{
                        inputs.D.url = "path:./flakeD";
                        outputs = { ... }: {};
                    }"""),
                    "flakeD": {"flake.nix": File("{ outputs = { ... }: {}; }")},
                },
            },
        }
    }
)
def test_follow_path_overloading(nix: Nix, files: Path, git: Git):
    r"""
    This tests a lockfile checking regression https://github.com/NixOS/nix/pull/8819

    We construct the following graph, where p->q means p has input q.
    A double edge means that the edge gets overridden using `follows`.

         A
        / \
       /   \
      v     v
      B ==> C   --- follows declared in A
       \\  /
        \\/     --- follows declared in B
         v
         D

    The message was
       error: input 'B/D' follows a non-existent input 'B/C/D'

    Note that for `B` to resolve its follow for `D`, it needs `C/D`,
    for which it needs to resolve the follow on `C` first.
    """

    git(files / "flakeA", "add", ".")

    nix.nix(["flake", "metadata", f"{files}/flakeA"]).run().ok()
    nix.nix(["flake", "update", "--flake", f"{files}/flakeA"]).run().ok()
    nix.nix(["flake", "lock", f"{files}/flakeA"]).run().ok()

    lockfile = json.loads((files / "flakeA/flake.lock").read_text())
    assert lockfile["nodes"]["B"]["inputs"] == {"C": ["C"], "D": ["B", "C", "D"]}
    assert lockfile["nodes"]["C"]["inputs"] == {"D": "D"}
    assert lockfile["nodes"]["root"]["inputs"] == {"B": "B", "C": "C"}


@with_files(
    {
        "flakeA": {
            "flake.nix": EnvTemplate("""{
                inputs.B.url = "path:@HOME@/flakeA/flakeB";
                inputs.D.url = "path:@HOME@/flakeA/flakeD";
                inputs.B.inputs.C.inputs.D.follows = "D";
                outputs = _: {};
            }"""),
            "flakeB": {
                "flake.nix": EnvTemplate("""{
                    inputs.C.url = "path:@HOME@/flakeA/flakeB/flakeC";
                    outputs = _: {};
                }"""),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                      inputs.D.url = "path:nosuchflake";
                      outputs = _: {};
                    }""")
                },
            },
            "flakeD": {"flake.nix": File("{ outputs = _: {}; }")},
        }
    }
)
def test_nested_overrides(nix: Nix, files: Path):
    nix.nix(["flake", "lock", f"{files}/flakeA"]).run().ok()
    lockfile = json.loads((files / "flakeA/flake.lock").read_text())
    assert lockfile["nodes"]["C"]["inputs"]["D"] == ["D"]


@with_files(
    {
        "flakeA": {
            "flake.nix": EnvTemplate("""{
                inputs.B.url = "path:@HOME@/flakeA/flakeB";
                inputs.C.url = "path:@HOME@/flakeA/flakeB/flakeC";
                inputs.B.inputs.C.follows = "C";
                outputs = _: {};
            }"""),
            "flakeB": {
                "flake.nix": EnvTemplate("""{
                    inputs.C.url = "path:nosuchflake";
                    inputs.D.url = "path:nosuchflake";
                    inputs.D.follows = "C/D";
                    outputs = _: {};
                }"""),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                      inputs.D.url = "path:@HOME@/flakeA/flakeD";
                      outputs = _: {};
                    }""")
                },
            },
            "flakeD": {"flake.nix": File("{ outputs = _: {}; }")},
        }
    }
)
def test_overlapping_flake_follows(nix: Nix, files: Path):
    # bug was not triggered without recreating the lockfile
    nix.nix(["flake", "update", "--flake", f"{files}/flakeA"]).run().ok()
    lockfile = json.loads((files / "flakeA/flake.lock").read_text())
    assert lockfile["nodes"]["B"]["inputs"]["D"] == ["B", "C", "D"]


@with_files(
    {
        "flakeA": {
            "flake.nix": EnvTemplate("""{
                inputs.B.url = "path:@HOME@/flakeA/flakeB";
                inputs.C.url = "path:@HOME@/flakeA/flakeB/flakeC";
                inputs.B.inputs.C.inputs.E.follows = "C";
                outputs = _: {};
            }"""),
            "flakeB": {
                "flake.nix": EnvTemplate("""{
                    inputs.C.url = "path:@HOME@/flakeA/flakeB/flakeC";
                    outputs = _: {};
                }"""),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                      inputs.D.url = "path:@HOME@/flakeA/flakeD";
                      outputs = _: {};
                    }""")
                },
            },
            "flakeD": {"flake.nix": File("{ outputs = _: {}; }")},
        }
    }
)
def test_override_nonexistant_input(nix: Nix, files: Path):
    """Test overlapping flake follows: B has D follow C/D, while A has B/C follow C"""
    out = nix.nix(["flake", "update", "--flake", f"{files}/flakeA"]).run().ok()
    assert "warning: input 'B/C' has an override for a non-existent input 'E'" in out.stderr_s


@with_files(
    {
        "flakeA": {
            "flake.nix": EnvTemplate("""{
                inputs = {
                  B.url = "path:@HOME@/flakeA/flakeB";
                  C = {
                    url = "path:@HOME@/flakeA/flakeB/flakeC";
                    inputs.D.inputs.E.follows = "B";
                  };
                };
                outputs = _: {};
            }"""),
            "flakeB": {
                "flake.nix": File("{ outputs = _: {}; }"),
                "flakeC": {
                    "flake.nix": EnvTemplate("""{
                      inputs.D.url = "path:@HOME@/flakeA/flakeD";
                      outputs = _: {};
                    }""")
                },
            },
            "flakeD": {
                "flake.nix": File("""{
                    inputs.E.url = "path:@HOME@/flakeA/flakeE";
                    outputs = _: {};
                }""")
            },
            "flakeE": {"flake.nix": File("{ outputs = { ... }: {}; }")},
        }
    }
)
def test_dep_fetch(nix: Nix, files: Path):
    """Test for Nested follows cause flake interactions to update the nested input #460"""

    # Lockfiles are cleared, initially the dependency needs to be fetched.
    out = nix.nix(["--verbose", "flake", "show", f"path:{files}/flakeA"]).run().ok()
    assert fnmatch.fnmatch(out.stderr_s, "*\nfetching path input 'path:*/flakeD'\n*")

    # But on another flake command it doesn't.
    out = nix.nix(["--verbose", "flake", "show", f"path:{files}/flakeA"]).run().ok()
    assert not fnmatch.fnmatch(out.stderr_s, "*\nfetching path input 'path:*/flakeD'\n*")

    # Make sure the nested override is actually correct in this testcase.
    metadata = nix.nix(["flake", "metadata", f"path:{files}/flakeA", "--json"]).run().json()
    assert metadata["locks"]["nodes"]["D"]["inputs"]["E"] == ["B"]
