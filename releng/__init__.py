from xonsh.main import setup
setup()
del setup

from . import environment
from . import create_release
from . import keys
from . import version
from . import cli
from . import docker

def reload():
    import importlib
    importlib.reload(environment)
    importlib.reload(create_release)
    importlib.reload(keys)
    importlib.reload(version)
    importlib.reload(cli)
    importlib.reload(docker)
