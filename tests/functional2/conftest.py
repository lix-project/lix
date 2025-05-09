import pytest
from pathlib import Path
from .testlib import fixtures


@pytest.fixture
def nix(tmp_path: Path):
    return fixtures.Nix(tmp_path)


pytest_plugins = (
    "functional2.testlib.fixtures.file_helper",
    "functional2.testlib.fixtures.formatter",
)
