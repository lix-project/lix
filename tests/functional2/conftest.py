import pytest
from pathlib import Path
from .testlib import fixtures


@pytest.fixture
def nix(tmp_path: Path):
    return fixtures.Nix(tmp_path)
