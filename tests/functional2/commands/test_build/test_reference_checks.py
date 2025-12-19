from functional2.testlib.fixtures.file_helper import with_files, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset_pack

import pytest

_files = {
    "check-refs.nix": CopyFile("assets/test_reference_checks/check-refs.nix"),
    **get_global_asset_pack("dependencies"),
}


@with_files(_files)
def test_references_detected(nix: Nix):
    dep, test1, test2 = (
        nix.nix_build(["check-refs.nix", "-A", "dep", "-A", "test1", "-A", "test2"])
        .run()
        .ok()
        .stdout_plain.splitlines()
    )

    refs_test1, refs_test2 = (
        nix.nix_store(["-q", "--references", path], build=True).run().ok().stdout_plain.splitlines()
        for path in [test1, test2]
    )

    assert dep in refs_test1
    assert test1 not in refs_test1

    assert dep not in refs_test2
    assert test2 not in refs_test2

    # test2 has a reference to file `aux-ref` (${src})
    assert any("aux-ref" in p for p in refs_test2)


@with_files(_files)
def test_allowed_references_error(nix: Nix):
    # empty `allowedReferences` throws on detected references
    error = nix.nix_build(["check-refs.nix", "-A", "test3"]).run().expect(1).stderr_plain
    assert "check-refs-3' is not allowed to have direct references to the following paths:" in error


@with_files(_files)
@pytest.mark.parametrize("ok_attr", ["test4", "test5", "test7"])
def test_allowed_references_ok(nix: Nix, ok_attr: str):
    # two derivations where the path matches allowedReferences
    nix.nix_build(["check-refs.nix", "-A", ok_attr]).run().ok()


@with_files(_files)
def test_allowed_reference_self_reference(nix: Nix):
    # self-reference that's not allowed
    error = (
        nix.nix_build(["check-refs.nix", "-A", "test6"]).run().expect(1).stderr_plain.splitlines()
    )
    assert (
        "check-refs-6' is not allowed to have direct references to the following paths:"
        in error[-2]
    )
    assert "check-refs-6" in error[-1]


@with_files(_files)
def test_tofile_no_drv_output(nix: Nix):
    error = nix.nix_build(["check-refs.nix", "-A", "test8"]).run().expect(1).stderr_plain

    assert (
        "error: files created by builtins.toFile may not reference derivations, but builder.sh references"
        in error
    )


@with_files(_files)
def test_disallowed_references(nix: Nix):
    # disallowedReferences error fires
    error = (
        nix.nix_build(["check-refs.nix", "-A", "test9"]).run().expect(1).stderr_plain.splitlines()
    )

    assert (
        "check-refs-9' is not allowed to have direct references to the following paths" in error[-2]
    )
    assert "dependencies-top" in error[-1]

    # Dependencies from `disallowedReferences` are used at build-time, but are not
    # referenced by the final store path -> success.
    nix.nix_build(["check-refs.nix", "-A", "test10"]).run().ok()


@with_files(_files)
def test_structured_attrs_discard(nix: Nix):
    result = nix.nix_build(["check-refs.nix", "-A", "test11"]).run().ok().stdout_plain.splitlines()

    assert not (
        nix.nix_store(["-q", "--references", *result], build=True)
        .run()
        .ok()
        .stdout_plain.splitlines()
    )


@with_files(_files)
def test_reference_specifier(nix: Nix):
    error = (
        nix.nix_build(["check-refs.nix", "-A", "test12"]).run().expect(1).stderr_plain.splitlines()
    )

    assert "output check for 'lib' contains an illegal reference specifier 'dev'" in error[-1]


@with_files(
    get_global_asset_pack("dependencies")
    | {
        "regression-reference-checks.nix": CopyFile(
            "assets/test_reference_checks/regression-reference-checks.nix"
        )
    }
)
def test_regression_partial_build(nix: Nix):
    out, _ = (
        nix.nix_build(
            ["regression-reference-checks.nix", "-A", "out", "-A", "man", "--no-out-link"]
        )
        .run()
        .ok()
        .stdout_plain.splitlines()
    )

    nix.nix_store(["--delete", out], build=True).run().ok()

    nix.nix_build(["regression-reference-checks.nix", "-A", "out"]).run().ok()
