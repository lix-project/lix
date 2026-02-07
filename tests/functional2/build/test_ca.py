import pytest
import re

from pathlib import Path

from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import Symlink, File, CopyFile

from typing import Literal

from dataclasses import dataclass


@dataclass
class CertAssessment:
    warnings: list[str]
    outcomes: set[Literal["missing", "present", "present-env-var", "corrupted"]]


certificate_files = {
    "cert": File("CERT_CONTENT"),
    "symlinked-cert": Symlink("cert"),
    "certificate-test.nix": CopyFile("assets/certificate-test.nix"),
    "config.nix": get_global_asset("config.nix"),
}


def assess_cert_presence_in_builds(
    nix: Nix,
    mode: Literal["normal", "fixed-output", "clobbering-impurities"],
    *,
    cert: Path | str | None = None,
    sandboxed: bool = True,
) -> CertAssessment:
    """
    Run a build with some special Nix code that makes use of the certificate authority code.
    We use the stdout to inspect whether our expectations are met.
    We return a list of known metadata about how the build ran.
    """
    expected_ret_code = 1 if mode in ("fixed-output", "clobbering-impurities") else 100

    res = (
        nix.nix_build(
            [
                "certificate-test.nix",
                "--argstr",
                "mode",
                mode,
                "--arg",
                "sandbox",
                "true" if sandboxed else "false",
                "--no-out-link",
            ]
            + (["--option", "ssl-cert-file", str(cert)] if cert is not None else [])
        )
        .run()
        .expect(expected_ret_code)
    )

    contains_clobbering_warning = (
        "warning: 'NIX_SSL_CERT_FILE is an impure environment variable" in res.stderr_plain
    )

    # this should be read as: mode == "clobbering-impurities" implies contains_clobbering_warning
    assert not contains_clobbering_warning or mode == "clobbering-impurities"

    outcomes = set(re.findall(r"CERT_(.*?)_IN_SANDBOX", res.stderr_plain))
    warnings = re.findall(r"warning: (.*)", res.stderr_plain)

    return CertAssessment(outcomes=outcomes, warnings=warnings)


@with_files(certificate_files)
@pytest.mark.parametrize("sandboxed", [False, pytest.param(True, marks=pytest.mark.full_sandbox)])
def test_missing_cert_in_ia_builds(nix: Nix, sandboxed: bool):
    assert assess_cert_presence_in_builds(
        nix, "normal", cert="cert", sandboxed=sandboxed
    ).outcomes == {"missing"}


@with_files(certificate_files)
@pytest.mark.parametrize("cert_path", ["", "/nowhere"])
@pytest.mark.parametrize("sandboxed", [False, pytest.param(True, marks=pytest.mark.full_sandbox)])
def test_missing_cert_in_fod_builds(nix: Nix, sandboxed: bool, cert_path: str):
    assert assess_cert_presence_in_builds(
        nix, "fixed-output", cert=cert_path, sandboxed=sandboxed
    ).outcomes == {"missing"}


@with_files(certificate_files)
@pytest.mark.parametrize("cert_path", ["cert", "symlinked-cert"])
@pytest.mark.parametrize("sandboxed", [False, pytest.param(True, marks=pytest.mark.full_sandbox)])
def test_presence_cert_in_fod_builds(nix: Nix, cert_path: str, sandboxed: bool):
    assert assess_cert_presence_in_builds(
        nix, "fixed-output", cert=cert_path, sandboxed=sandboxed
    ).outcomes == ({"present", "present-env-var"} if sandboxed else {"present-env-var"})

    nix.env["NIX_SSL_CERT_FILE"] = str(cert_path)
    assert assess_cert_presence_in_builds(nix, "fixed-output", sandboxed=sandboxed).outcomes == (
        {"present", "present-env-var"} if sandboxed else {"present-env-var"}
    )


@with_files(certificate_files)
@pytest.mark.parametrize("sandboxed", [False, pytest.param(True, marks=pytest.mark.full_sandbox)])
class TestCertClobberingInFODs:
    def test_no_env(self, nix: Nix, sandboxed: bool):
        # there's no NIX_SSL_CERT_FILE presently, so everything works as usually.
        assert assess_cert_presence_in_builds(
            nix, "clobbering-impurities", cert="cert", sandboxed=sandboxed
        ).outcomes == ({"present", "present-env-var"} if sandboxed else {"present-env-var"})

    def test_with_env_with_flag(self, nix: Nix, sandboxed: bool):
        # NIX_SSL_CERT_FILE is set but we pass --ssl-cert-file which takes precedence.
        # We expect warnings to occur.
        nix.env["NIX_SSL_CERT_FILE"] = "/nowhere"
        assert assess_cert_presence_in_builds(
            nix, "clobbering-impurities", cert="cert", sandboxed=sandboxed
        ).outcomes == ({"present", "present-env-var"} if sandboxed else {"present-env-var"})

    def test_with_env_no_flag(self, nix: Nix, sandboxed: bool):
        # NIX_SSL_CERT_FILE is set but we do not pass any flag.
        # We expect no warning.
        nix.env["NIX_SSL_CERT_FILE"] = "/nowhere"

        assert assess_cert_presence_in_builds(
            nix, "clobbering-impurities", sandboxed=sandboxed
        ).outcomes == {"missing"}

    def test_with_valid_env_no_flag(self, nix: Nix, sandboxed: bool):
        # We change the environment variable to a valid path.
        nix.env["NIX_SSL_CERT_FILE"] = "cert"
        assert assess_cert_presence_in_builds(
            nix, "clobbering-impurities", sandboxed=sandboxed
        ).outcomes == ({"present", "present-env-var"} if sandboxed else {"present-env-var"})

    def test_warning_absence(self, nix: Nix, sandboxed: bool):
        warning_prefix = "'NIX_SSL_CERT_FILE' is an impure environment variable"

        # Under impureEnvVars set, Lix will set `NIX_SSL_CERT_FILE` only if it existed in the environment
        # of the builder.
        # This should cause the warning to be absent.
        nix.env.unset_env("NIX_SSL_CERT_FILE")
        assessment = assess_cert_presence_in_builds(
            nix, "clobbering-impurities", cert="cert", sandboxed=sandboxed
        )

        assert not any(w.startswith(warning_prefix) for w in assessment.warnings)
