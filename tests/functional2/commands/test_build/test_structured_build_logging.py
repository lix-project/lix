import logging
from testlib.fixtures.file_helper import CopyFile, with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


_files = {
    "config.nix": get_global_asset("config.nix"),
    "unusual-logging.nix": CopyFile("assets/test_structured_build_logging/unusual-logging.nix"),
}

_INVALID_LOG_WARNING = "message from the derivation builder: "


@with_files(_files)
def test_bad_logs(nix: Nix, logger: logging.Logger):
    nix.settings.add_xp_feature("nix-command")
    expected_errors = (
        nix.nix(["eval", "--json", "-f", "unusual-logging.nix", "normalInvalidLogs"])
        .run()
        .ok()
        .json()
    )

    res = nix.nix_build(["unusual-logging.nix", "-A", "normalInvalid"])
    errs = res.run().stderr_plain
    logger.debug("err: %s", errs)
    assert errs.count(_INVALID_LOG_WARNING) == len(expected_errors)

    # we expect that the error message contains the offending log line
    for err in expected_errors:
        assert err in errs


@with_files(_files)
def test_invalid_fields(nix: Nix, logger: logging.Logger):
    nix.settings.add_xp_feature("nix-command")
    expected_error = (
        nix.nix(["eval", "--json", "-f", "unusual-logging.nix", "invalidFieldsLog"])
        .run()
        .ok()
        .json()
    )

    res = nix.nix_build(["unusual-logging.nix", "-A", "invalidFields"])
    errs = res.run().stderr_plain
    logger.debug("err: %s", errs)
    assert _INVALID_LOG_WARNING in errs
    assert expected_error in errs
