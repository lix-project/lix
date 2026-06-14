from testlib.utils import get_global_asset_pack
from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.file_helper import CopyFile
from testlib.fixtures.nix import Nix

export_files = {"export-graph.nix": CopyFile("assets/export-graph.nix")} | get_global_asset_pack(
    "dependencies"
)


def check_ref(nix: Nix, file: str):
    res = nix.nix_store(["-q", "--references", nix.env.dirs.home / "result"]).run().ok()
    assert file in res.stdout_plain


def check_out_path_refs(nix: Nix, out_path: str):
    for file in Path(out_path).read_text().splitlines():
        check_ref(nix, file)


@with_files(export_files)
def test_export_runtime(nix: Nix, files: Path):
    result = files / "result"
    out_path = (
        nix.nix_build(["./export-graph.nix", "-A", 'foo."bar.runtimeGraph"', "-o", result])
        .run()
        .ok()
    )
    references = nix.nix_store(["-q", "--references", result]).run().ok().stdout_plain
    assert len(references.splitlines()) == 4

    check_ref(nix, "input-2")
    check_out_path_refs(nix, out_path.stdout_plain)


@with_files(export_files)
def test_built_time(nix: Nix, files: Path):
    result = files / "result"
    out_path = (
        nix.nix_build(["./export-graph.nix", "-A", 'foo."bar.buildGraph"', "-o", result]).run().ok()
    )

    for file in ["input-1", "input-1.drv", "input-2", "input-2.drv"]:
        check_ref(nix, file)
    check_out_path_refs(nix, out_path.stdout_plain)
