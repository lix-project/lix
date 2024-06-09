from xonsh.main import setup
setup()
del setup

import logging

from . import environment
from . import create_release
from . import keys
from . import version
from . import cli
from . import docker
from . import docker_assemble
from . import gitutils

rootLogger = logging.getLogger()
rootLogger.setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG)

fmt = logging.Formatter('{asctime} {levelname} {name}: {message}',
                        datefmt='%b %d %H:%M:%S',
                        style='{')

if not any(isinstance(h, logging.StreamHandler) for h in rootLogger.handlers):
    hand = logging.StreamHandler()
    hand.setFormatter(fmt)
    rootLogger.addHandler(hand)

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
