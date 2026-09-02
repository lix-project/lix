from testlib.fixtures.nix import Nix, NixDaemon
from testlib.fixtures.file_helper import with_files, File
from testlib.utils import get_global_asset

import os
import signal
import pytest


@with_files(
    {
        "config.nix": get_global_asset("config.nix"),
        "expr.nix": File("""
            { fifo }:
            with import ./config.nix;

            mkDerivation {
                name = "cancel-test";
                buildCommand = ''
                  echo -n started > ${fifo}
                  cat ${fifo}
                '';
            }
        """),
    }
)
@pytest.mark.timeout(30)
def test_cancels_are_timely(nix: Nix, daemon: NixDaemon):
    nix.settings.add_xp_feature("nix-command")
    fifo = nix.env.dirs.home / "fifo"
    os.mkfifo(fifo)
    nix.settings["extra-sandbox-paths"] = [str(fifo.parent)]

    with daemon(nix) as inner:
        cmd = inner.nix(["build", "-f", "./expr.nix", "--argstr", "fifo", fifo]).start()
        assert fifo.read_text() == "started"
        # don't write to the fifo, that'd unblock the build
        cmd._proc.send_signal(signal.SIGINT)
        assert "interrupted by the user" in cmd.wait().expect(1).stderr_plain
