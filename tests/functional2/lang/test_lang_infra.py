import string
from collections.abc import Callable
from pathlib import Path
from textwrap import dedent

import pytest

from functional2.testlib.fixtures.command import Command
from functional2.testlib.fixtures.file_helper import File, AssetSymlink, with_files
from functional2.testlib.fixtures.snapshot import Snapshot
from functional2.testlib.utils import get_functional2_lang_files


@pytest.mark.parametrize("pytest_command", [["-k", "generic_test", "--setup-plan"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {"generic_test": {"in.nix": File("{}"), "eval-okay.out.exp": File("{ }\n")}}
            }
        }
    )
)
def test_detects_generic_lang_test(pytest_command: Command):
    result = pytest_command.run().ok()
    assert "lang/test_lang.py::test_eval[generic_test:eval-okay]" in result.stdout_plain


@pytest.mark.parametrize("pytest_command", [["-k", "toml_test", "--setup-plan"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_test": {
                        "in.nix": File("{}"),
                        "my_name.out.exp": File("{ }\n"),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    name = "my_name"
                                    runner = "eval-okay"
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_detects_toml_lang_test(pytest_command: Command):
    result = pytest_command.run().ok()
    assert "lang/test_lang.py::test_eval[toml_test:my_name]" in result.stdout_plain


@pytest.mark.parametrize("pytest_command", [["--setup-plan"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "some_py_module": {
                        "in.nix": File("{}"),
                        "eval-okay.out.exp": File("{ }\n"),
                        "__init__.py": File(""),
                    }
                }
            }
        }
    )
)
def test_skips_py_files(files: Path, pytest_command: Command):
    result = pytest_command.run().ok()
    assert "lang/test_lang.py::test_eval[some_py_module:eval-okay]" not in result.stdout_plain
    assert (
        f"[    INFO] [lang-test-collector] skipping {files.absolute()}/functional2/lang/some_py_module as it contains a py file, assuming custom tests"
        in result.stdout_plain
    )


@pytest.mark.parametrize("pytest_command", [["-k", "n_suffix"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "n_suffix": {
                        "in-1.nix": File("{}"),
                        "in-2.nix": File("{}"),
                        "eval-okay-1.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay-2.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_collects_with_n_suffix(pytest_command: Command):
    result = pytest_command.run().ok()
    out = result.stdout_plain
    assert "n_suffix:eval-okay-1" in out
    assert "n_suffix:eval-okay-2" in out


@pytest.mark.parametrize("pytest_command", [["-k", "short_string_suffix"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "short_string_suffix": {
                        "in-speaker.nix": File("{}"),
                        "in-microphone.nix": File("{}"),
                        "eval-okay-speaker.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay-microphone.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_collects_short_string_suffix(pytest_command: Command):
    result = pytest_command.run().ok()
    out = result.stdout_plain
    assert "short_string_suffix:eval-okay-speaker" in out
    assert "short_string_suffix:eval-okay-microphone" in out


@pytest.mark.parametrize("pytest_command", [["-k", "dash_suffix"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "dash_suffix": {
                        "in-string-with-dash.nix": File("{}"),
                        "in-some-more--dashes.nix": File("{}"),
                        "eval-okay-string-with-dash.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay-some-more--dashes.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_collects_string_suffix_with_dash(pytest_command: Command):
    result = pytest_command.run().ok()
    out = result.stdout_plain
    assert "dash_suffix:eval-okay-string-with-dash" in out
    assert "dash_suffix:eval-okay-some-more--dashes" in out
    assert "dash_suffix:eval-okay-" in out


@pytest.mark.parametrize("pytest_command", [["-k", "bad_naming"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "bad_naming": {
                        "in-&x.nix": File("{}"),
                        "in-.nix": File("{}"),
                        "eval-okay-&x.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay-.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_collection_fails_with_bad_naming(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_plain
    assert "bad_naming:eval-okay" in err
    assert "incorrectly formatted test name: 'eval-okay-'" in err
    assert "incorrectly formatted test name: 'eval-okay-&x'" in err


@pytest.mark.parametrize("pytest_command", [["-k", "infra and runners"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "infra_okay_runners": {
                        "in.nix": File("{}"),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "parse-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_po.out.exp"
                        ),
                    },
                    "infra_fail_runners": {
                        "in.nix": File("{"),
                        "eval-fail.err.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_ef.err.exp"
                        ),
                        "parse-fail.err.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_pf.err.exp"
                        ),
                    },
                }
            }
        }
    )
)
def test_all_runners_work(pytest_command: Command):
    result = pytest_command.run().ok()
    assert "lang/test_lang.py::test_eval[infra_okay_runners:eval-okay]" in result.stdout_plain
    assert "lang/test_lang.py::test_parser[infra_okay_runners:parse-okay]" in result.stdout_plain
    assert "lang/test_lang.py::test_xfail_eval[infra_fail_runners:eval-fail]" in result.stdout_plain
    assert (
        "lang/test_lang.py::test_xfail_parser[infra_fail_runners:parse-fail]" in result.stdout_plain
    )


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "generic_bad": {
                        "in.nix": File("{}"),
                        "fee-foo.out.exp": File(""),
                        "hello-okay.out.exp": File(""),
                        "parse-fops.out.exp": File(""),
                    }
                }
            }
        }
    )
)
def test_generic_bad_runner_name(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_s
    assert "test_invalid_configuration" in err
    assert "invalid runner name:" in err
    assert "Invalid configuration for 'generic_bad:fee-foo'" in err
    assert "Invalid configuration for 'generic_bad:hello-okay'" in err
    assert "Invalid configuration for 'generic_bad:parse-fops'" in err


@pytest.mark.parametrize("pytest_command", [["-k", "toml_test"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_test": {
                        "in.nix": File("{}"),
                        "my_name.out.exp": File("{ }\n"),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    name = "my_name"
                                    runner = "plushies"
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_bad_runner_name(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_plain
    assert "test_invalid_configuration" in err
    assert "invalid runner name:" in err
    assert "Invalid configuration for 'toml_test:my_name':" in err


@pytest.mark.parametrize("pytest_command", [["-k", "toml_test"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_test": {
                        "in.nix": File("{}"),
                        "my_name.out.exp": File("{ }\n"),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    name = "my_name"
                                    runner = "eval-okay"
                                    cuddles = true
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_too_many_args(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_plain
    assert "test_invalid_configuration" in err
    assert "unexpected arguments: ['cuddles']" in err


@pytest.mark.parametrize("pytest_command", [["-k", "toml_test"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_test": {
                        "in.nix": File("{}"),
                        "my_name.out.exp": File("{ }\n"),
                        "test.toml": File(
                            dedent("""
                                    invalid_test = "eval-okay"
                                    [[test]]
                                    name = "my_name"
                                    runner = "eval-okay"
                                    flags = 1
                                    [[test]]
                                    name = "second"
                                    runner = "eval-okay"
                                    extra-files = [false, true, true]
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_invalid_argument_types(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_plain
    assert "test_invalid_configuration" in err
    assert "unexpected key(s) ['invalid_test']; expected only 'test'" in err
    assert "invalid value type for 'flags'" in err
    assert "invalid value type for 'extra_files':" in err


@pytest.mark.parametrize("pytest_command", [["-k", "toml_test"]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_test": {
                        "in.nix": File("{}"),
                        "my_name.out.exp": File("{ }\n"),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    name = "my_name"
                                    runner = "eval-okay"
                                    cuddles = True
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_invalid_toml(pytest_command: Command):
    result = pytest_command.run().expect(1)
    err = result.stdout_plain
    assert "test_invalid_configuration" in err
    assert "couldn't parse toml" in err


@pytest.mark.parametrize(
    "pytest_command", [(["-k", "update_test", "--accept-tests"], False)], indirect=True
)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {"update_test": {"in.nix": File("{}"), "eval-okay.out.exp": File("old")}}
            },
            "out.exp": AssetSymlink("assets/test_lang_infra/runner_eo.out.exp"),
        }
    )
)
def test_updates_expected_output(
    files: Path, pytest_command: Command, snapshot: Callable[[str], Snapshot]
):
    assert (files / "functional2/lang/update_test/eval-okay.out.exp").read_text() == "old"
    pytest_command.run().ok()
    assert (
        snapshot("out.exp")
        == (files / "functional2/lang/update_test/eval-okay.out.exp").read_text()
    )


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "generic_unused": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_generic_throws_unused_files(pytest_command: Command):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "test_invalid_configuration[generic_unused-reasons0]" in out
    assert "the following files weren't referenced: {'in-1.nix'}" in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_unused": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-fail.err.exp": File(""),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    runner = "eval-okay"
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_throws_unused_files(
    pytest_command: Command, balanced_templater: type[string.Template]
):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "test_invalid_configuration[toml_unused-reasons0]" in out
    pattern = balanced_templater("the following files weren't referenced: {'@A@', '@B@'}")
    assert pattern.substitute(A="eval-fail.err.exp", B="in-1.nix") in out or pattern.substitute(
        B="eval-fail.err.exp", A="in-1.nix"
    )


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml_unused": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-fail.err.exp": File(""),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                    [[test]]
                                    runner = "eval-okay"
                                    [[test]]
                                    runner = "bad-name"
                                    """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_no_unused_on_invalid_config(pytest_command: Command):
    res = pytest_command.run().expect(1)
    log = res.stdout_plain
    assert "test_invalid_configuration[toml_unused:bad-name-reasons0]" in log
    assert "invalid runner name:" in log
    assert "the following files weren't referenced" not in log


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "generic_unused": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "shrimpy-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                    }
                }
            }
        }
    )
)
def test_generic_no_unused_on_invalid_config(pytest_command: Command):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "test_invalid_configuration[generic_unused:shrimpy-okay-reasons0]" in out
    assert "invalid runner name:" in out
    assert "the following files weren't referenced" not in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "single-multi": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-fail.err.exp": File(""),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        runner = "eval-okay"
                                        in = ["in.nix", "in-1.nix"]
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_single_only_str(pytest_command: Command):
    res = pytest_command.run().expect(1)
    err = res.stdout_plain
    assert "test_invalid_configuration[single-multi:eval-okay-reasons0]" in err
    assert "invalid type for 'in': ['in.nix', 'in-1.nix'], expected a string" in err


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "multi-single": {
                        "in.nix": File("{}"),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        matrix = true
                                        runner = "eval-okay"
                                        in = "in.nix"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_matrix_only_list_str(pytest_command: Command):
    res = pytest_command.run().expect(1)
    err = res.stdout_plain
    assert "test_invalid_configuration[multi-single:eval-okay-reasons0]" in err
    assert "invalid type for 'in': 'in.nix', expected a list of string" in err


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "matrix-all": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-okay-1.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        matrix = true
                                        runner = "eval-okay"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_matrix_uses_all_files(pytest_command: Command):
    res = pytest_command.run().ok()
    out = res.stdout_plain
    assert "test_eval[matrix-all:eval-okay] PASSED" in out
    assert "test_eval[matrix-all:eval-okay-1] PASSED" in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "mixed-matrix": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "eval-okay-1.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "non-matrix-1.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        matrix = true
                                        runner = "eval-okay"
                                        [[test]]
                                        name = "non-matrix"
                                        in = "in-1.nix"
                                        runner = "eval-okay"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_mixing_matrix_single(pytest_command: Command):
    res = pytest_command.run().ok()
    out = res.stdout_plain
    assert "test_eval[mixed-matrix:eval-okay] PASSED" in out
    assert "test_eval[mixed-matrix:eval-okay-1] PASSED" in out
    assert "test_eval[mixed-matrix:non-matrix-1] PASSED" in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "non-unique": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        matrix = true
                                        runner = "eval-okay"
                                        [[test]]
                                        in = "in-1.nix"
                                        runner = "eval-okay"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_non_unique_name(pytest_command: Command):
    res = pytest_command.run().expect(1)
    err = res.stdout_plain
    assert "test_invalid_configuration[non-unique:eval-okay-1-reasons0]" in err
    assert (
        "id 'non-unique:eval-okay-1' is not unique. Please set the 'name' attribute manually" in err
    )


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "subset": {
                        "in.nix": File("{}"),
                        "in-1.nix": File("{}"),
                        "in-hello.nix": File("{}"),
                        "eval-okay-1.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "matrix.out.exp": AssetSymlink("assets/test_lang_infra/runner_eo.out.exp"),
                        "matrix-hello.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        name = "matrix"
                                        matrix = true
                                        in = ["in.nix", "in-hello.nix"]
                                        runner = "eval-okay"
                                        [[test]]
                                        runner = "eval-okay"
                                        in = "in-1.nix"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_matrix_subset(pytest_command: Command):
    res = pytest_command.run().ok()
    out = res.stdout_plain
    assert "test_eval[subset:matrix] PASSED" in out
    assert "test_eval[subset:matrix-hello] PASSED" in out
    assert "test_eval[subset:matrix-1] PASSED" not in out
    assert "test_eval[subset:eval-okay-1] PASSED" in out
    assert "test_eval[subset:eval-okay] PASSED" not in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "in-naming": {
                        "in-some-name.nix": File("{}"),
                        "eval-okay-some-name.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        matrix = true
                                        in = ["in-some-name", "-some-name.nix", "some-name.nix"]
                                        runner = "eval-okay"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_bad_in_naming(pytest_command: Command):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "test_invalid_configuration[in-naming:eval-okay-reasons0]" in out
    for msg in [
        "invalid in-file name 'in-some-name' at position 0 for 'in'",
        "invalid in-file name '-some-name.nix' at position 1 for 'in'",
        "invalid in-file name 'some-name.nix' at position 2 for 'in'",
    ]:
        assert msg in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "toml-missing": {
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        ),
                        "test.toml": File(
                            dedent("""
                                        [[test]]
                                        runner = "eval-okay"
                                        """)
                        ),
                    }
                }
            }
        }
    )
)
def test_toml_missing_in_file(pytest_command: Command):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "ERROR lang/test_lang.py::test_eval[toml-missing:eval-okay] - FileNotFound" in out


@pytest.mark.parametrize("pytest_command", [[]], indirect=True)
@with_files(
    get_functional2_lang_files(
        {
            "functional2": {
                "lang": {
                    "generic-missing": {
                        "eval-okay.out.exp": AssetSymlink(
                            "assets/test_lang_infra/runner_eo.out.exp"
                        )
                    }
                }
            }
        }
    )
)
def test_generic_missing_in_file(pytest_command: Command):
    res = pytest_command.run().expect(1)
    out = res.stdout_plain
    assert "ERROR lang/test_lang.py::test_eval[generic-missing:eval-okay] - FileNotFound" in out
