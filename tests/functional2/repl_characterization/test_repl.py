import re
from pathlib import Path

import pytest
from mistletoe.markdown_renderer import MarkdownRenderer

from testlib.repl_util import ReplTest, ReplTestMetadata, get_repl_test_params
from testlib.fixtures.nix import Nix


def pytest_generate_tests(metafunc: pytest.Metafunc):
    if metafunc.definition.name != "test_repl_char":
        return

    params, ids = get_repl_test_params()
    metafunc.parametrize(("files", "metadata"), params, indirect=["files"], ids=ids)


def _clean_output(output: str, origin: Path) -> str:
    lix_version_regex = r"Lix \d+\.\d+\.\d+-?[^\n ]*"
    return re.sub(lix_version_regex, "Lix VERSION", output.replace(str(origin), "/pwd"))


@pytest.mark.nix_settings(trusted_users="*")  # silence trusted settings warnings
def test_repl_char(nix: Nix, do_snapshot_update: bool, metadata: ReplTestMetadata, files: Path):
    nix.settings.add_xp_feature("nix-command", "flakes", "repl-automation")
    with MarkdownRenderer() as renderer:
        test: ReplTest = metadata.create_test()

        args = [arg.replace("{PWD}", str(files.absolute())) for arg in metadata.args or []]
        cmd = nix.nix(["repl", "--offline", *args]).with_stdin(test.input.encode())
        cmd.err_to_out = True
        res = cmd.run().expect(metadata.should_fail or 0)

        usable_output = _clean_output(res.stdout_plain, files)
        if test.check_and_update(usable_output, do_snapshot_update):
            new_content = metadata.as_frontmatter + renderer.render(test.doc)
            metadata.file.write_text(new_content)
            pytest.skip("Updated golden files")
