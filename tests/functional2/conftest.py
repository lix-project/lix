pytest_plugins = (
    "testlib.fixtures.env",
    "testlib.fixtures.formatter",
    "testlib.fixtures.file_helper",
    "testlib.fixtures.logger",
    "testlib.fixtures.command",
    "testlib.fixtures.git",
    "testlib.fixtures.nix",
    "testlib.fixtures.snapshot",
    "testlib.fixtures.pytest_command",
)

# the rest of this file is also a fork of pytest-tap v3.5 adjusted for lix use casess.
# ideally this would be its own module, but xdist causes failures if it is. dunno why.

# Copyright (c) 2021, Matt Layman and contributors. See AUTHORS for more details.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import sys

import pytest
from tap.formatter import format_as_diagnostics
from tap.tracker import Tracker

SHOW_CAPTURE_LOG = ("log", "all")
SHOW_CAPTURE_OUT = ("stdout", "all")
SHOW_CAPTUER_ERR = ("stderr", "all")

_is_xdist_worker = False

@pytest.hookimpl(optionalhook=True)
def pytest_configure_node(node):
    _is_xdist_worker = True

class TAPPlugin:
    def __init__(self, config: pytest.Config) -> None:
        self._tracker = Tracker(
            outdir=config.option.tap_outdir,
            combined=config.option.tap_combined,
            streaming=config.option.tap_stream,
            stream=sys.stdout,
        )

        if self._tracker.streaming and not _is_xdist_worker:
            reporter = config.pluginmanager.getplugin("terminalreporter")
            if reporter:
                config.pluginmanager.unregister(reporter)
            # A common pytest pattern is to use test functions without classes.
            # The header looks really dumb for that pattern because it puts
            # out a lot of line noise since every function gets its own header.
            # Disable it automatically for streaming.
            self._tracker.header = False

        self.show_capture = config.option.showcapture
        self._in_xdist = False
        self.log_passing_tests = config.option.tap_log_passing_tests

    def set_plan(self, tests):
        self._tracker.set_plan(tests)

    @pytest.hookimpl()
    def pytest_runtestloop(self, session):
        """Output the plan line first."""
        if self._in_xdist:
            return
        option = session.config.option
        if option.tap_stream or option.tap_combined:
            self.set_plan(session.testscollected)

    @pytest.hookimpl(optionalhook=True)
    def pytest_xdist_setupnodes(self, config, specs):
        """Ensure that we don't output an empty plan in the runtestloop hook if xdist is enabled"""
        self._in_xdist = True

    @pytest.hookimpl(optionalhook=True)
    def pytest_xdist_node_collection_finished(self, node, ids):
        """Output the plan line first when using xdist."""
        if self._tracker.streaming or self._tracker.combined:
            self.set_plan(len(ids))

    @pytest.hookimpl()
    def pytest_runtest_logreport(self, report: pytest.TestReport):
        """Add a test result to the tracker."""
        is_trackable_result = (
            (report.when == "setup" and report.outcome == "skipped")
            or (report.when == "setup" and report.outcome == "failed")
            or report.when == "call"
        )
        if not is_trackable_result:
            return

        description = str(report.location[0]) + "::" + str(report.location[2])
        testcase = report.location[0]

        # Handle xfails first because they report in unusual ways.
        # Non-strict xfails will include `wasxfail` while strict xfails won't.
        if hasattr(report, "wasxfail"):
            reason = ""
            # pytest adds an ugly "reason: " for expectedFailure
            # even though the standard library doesn't accept a reason
            # for that decorator.
            # Ignore the "reason: " from pytest.
            if report.wasxfail and report.wasxfail != "reason: ":
                reason = f": {report.wasxfail}"

            if report.skipped:
                directive = f"TODO expected failure{reason}"
                self._tracker.add_not_ok(testcase, description, directive=directive)
            elif report.passed:
                directive = f"TODO unexpected success{reason}"
                self._tracker.add_ok(testcase, description, directive=directive)
        elif report.passed:
            diagnostics = None
            if self.log_passing_tests:
                diagnostics = _make_as_diagnostics(report, self.show_capture)
            self._tracker.add_ok(testcase, description, diagnostics=diagnostics)
        elif report.failed:
            diagnostics = _make_as_diagnostics(report, self.show_capture)

            # pytest treats an unexpected success from unitest.expectedFailure
            # as a failure.
            # To match up with TAPTestResult and the TAP spec, treat the pass
            # as an ok with a todo directive instead.
            if "Unexpected success" in str(report.longrepr):
                self._tracker.add_ok(
                    testcase, description, directive="TODO unexpected success"
                )
                return

            # A strict xfail that passes (i.e., XPASS) should be marked as a failure.
            # The only indicator that strict xfail occurred
            # for XPASS is to check longrepr.
            if (
                isinstance(report.longrepr, str)
                and "[XPASS(strict)]" in report.longrepr
            ):
                self._tracker.add_not_ok(
                    testcase,
                    description,
                    directive=f"unexpected success: {report.longrepr}",
                )
                return

            self._tracker.add_not_ok(testcase, description, diagnostics=diagnostics)
        elif report.skipped:
            reason = report.longrepr[2].split(":", 1)[1].strip()  # type: ignore
            self._tracker.add_skip(testcase, description, reason)

        self._tracker.stream.flush()

    @pytest.hookimpl()
    def pytest_unconfigure(self, config: pytest.Config):
        """Dump the results."""
        self._tracker.generate_tap_reports()


def pytest_addoption(parser):
    """Include all the command line options."""
    group = parser.getgroup("terminal reporting", "reporting", after="general")
    group.addoption(
        "--tap",
        default=False,
        dest="tap_stream",
        action="store_true",
        help="Stream TAP output instead of the default test runner output.",
    )
    # Deprecated, but keeping for backwards compatibility.
    group.addoption(
        "--tap-stream", default=False, action="store_true", help=argparse.SUPPRESS
    )
    group.addoption(
        "--tap-files",
        default=False,
        action="store_true",
        help="Store all TAP test results into individual files per test case.",
    )
    group.addoption(
        "--tap-combined",
        default=False,
        action="store_true",
        help="Store all TAP test results into a combined output file.",
    )
    group.addoption(
        "--tap-outdir",
        metavar="path",
        help=(
            "An optional output directory to write TAP files to. "
            "If the directory does not exist, it will be created."
        ),
    )
    group.addoption(
        "--tap-log-passing-tests",
        default=False,
        action="store_true",
        help="Capture log information for passing tests to TAP report",
    )


@pytest.hookimpl(trylast=True)
def pytest_configure(config: pytest.Config) -> None:
    """Enable the plugin if the TAP flags are used."""
    # The help printing uses the terminalreporter,
    # which is unregistered by the streaming mode.
    if config.option.help:
        return

    if (
        config.option.tap_stream
        or config.option.tap_combined
        or config.option.tap_files
    ):
        config.pluginmanager.register(TAPPlugin(config), "tapplugin")


def _make_as_diagnostics(report, show_capture):
    """Format a report as TAP diagnostic output."""
    lines = report.longreprtext.splitlines(keepends=True)

    if show_capture in SHOW_CAPTURE_LOG:
        if lines:
            lines[-1] += "\n"
        lines += ["--- Captured Log ---\n"] + (
            report.caplog.splitlines(keepends=True) or [""]
        )
    if show_capture in SHOW_CAPTURE_OUT:
        if lines:
            lines[-1] += "\n"
        lines += ["--- Captured Out ---\n"] + (
            report.capstdout.splitlines(keepends=True) or [""]
        )
    if show_capture in SHOW_CAPTUER_ERR:
        if lines:
            lines[-1] += "\n"
        lines += ["--- Captured Err ---\n"] + (
            report.capstderr.splitlines(keepends=True) or [""]
        )

    return format_as_diagnostics(lines)
