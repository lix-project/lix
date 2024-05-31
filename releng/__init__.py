from xonsh.main import setup
setup()
del setup

from releng import environment
from releng import create_release
from releng import keys
from releng import version
from releng import cli

def reload():
    import importlib
    importlib.reload(environment)
    importlib.reload(create_release)
    importlib.reload(keys)
    importlib.reload(version)
    importlib.reload(cli)
