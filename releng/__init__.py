from xonsh.main import setup
import signal

# This is a workaround for https://github.com/xonsh/xonsh/issues/5244
# Matching https://github.com/xonsh/xonsh/blob/71d4920ac5c16f2ef37da88b3f563c25c585fb82/xonsh/main.py#L519-L525
def func_sig_ttin_ttou(n, f):
    pass

signal.signal(signal.SIGTTIN, func_sig_ttin_ttou)
signal.signal(signal.SIGTTOU, func_sig_ttin_ttou)

setup()
del setup

import logging
import sys

import xonsh.shells.base_shell

from . import environment
from . import create_release
from . import keys
from . import version
from . import cli
from . import docker
from . import docker_assemble
from . import release_notes
from . import gitutils


def setup_logging():
    """
    Sets up logging to work properly. The following are intended to work:
    - ipython/xonsh configuration files adding log handlers out of band
    - Reloading the module in xonsh/ipython not causing Bonus Loggers (which is
      why we check if there is already a handler. This also helps the previous
      case)
    - Importing the releng module from xonsh and poking at it interactively
    """

    LEVELS = {
        # Root logger must be DEBUG so that anything else can be DEBUG
        None: logging.DEBUG,
        # Everything in releng
        __name__: logging.DEBUG,
        # Log spam caused by prompt_toolkit
        'asyncio': logging.INFO,
    }

    for name, level in LEVELS.items():
        logger = logging.getLogger(name)
        logger.setLevel(level)

    root_logger = logging.getLogger()

    fmt = logging.Formatter('{asctime} {levelname} {name}: {message}',
                            datefmt='%b %d %H:%M:%S',
                            style='{')

    if not any(
            isinstance(h, logging.StreamHandler) for h in root_logger.handlers):
        stderr = sys.stderr
        # XXX: Horrible hack required by the virtual stderr xonsh uses for each entered
        # command getting closed after the command is run: we need to pull out
        # the real stderr because this survives across multiple command runs.
        #
        # This only applies when running xonsh in interactive mode and importing releng.
        if isinstance(sys.stderr, xonsh.shells.base_shell._TeeStd):
            stderr = stderr.std  # type: ignore

        hand = logging.StreamHandler(stream=stderr)
        hand.set_name('releng root handler')
        hand.setFormatter(fmt)
        root_logger.addHandler(hand)


setup_logging()


def reload():
    import importlib
    importlib.reload(environment)
    importlib.reload(create_release)
    importlib.reload(keys)
    importlib.reload(version)
    importlib.reload(cli)
    importlib.reload(docker)
    importlib.reload(docker_assemble)
    importlib.reload(gitutils)
    importlib.reload(release_notes)
