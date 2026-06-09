from textwrap import dedent
from mistletoe.markdown_renderer import BlankLine
from typing import Any
from _pytest.config import Config
import mistletoe
import dataclasses
import re
from dataclasses import dataclass
from pathlib import Path

import frontmatter
from mistletoe.block_token import BlockToken, CodeFence

from testlib.fixtures.file_helper import CopyFile, FileDeclaration
from testlib.utils import functional2_base_folder


def _add_output_codefence(input_elem: CodeFence) -> CodeFence:
    parent = input_elem.parent
    pos = parent.children.index(input_elem)
    match = ("", (input_elem.indentation, input_elem.delimiter, "output", input_elem.language))
    output_block = CodeFence(match)
    parent.children.insert(pos + 1, output_block)
    parent.children.insert(pos + 2, BlankLine("\n"))
    return output_block


def _create_startup_block() -> CodeFence:
    """
    Creates a codefence whose content is the default startup message of a repl
    """
    match = (
        dedent("""
            Lix VERSION
            Type :? for help.
        """),
        (0, "```", "output", "output"),
    )
    return ReplTestBlock("\n", CodeFence(match))


def _update_codefence(expected: CodeFence, actual: str):
    expected = expected.output
    # HACK(rootile, 2026-05): The \n is required due to the renderer seeming to have an off-by-one error resulting in the deletion of the last character :melt:
    appendix = "" if actual.endswith("\n") else "\n"
    expected.children[0].content = actual + appendix
    delimiter = expected.delimiter[0]
    delimiter_length = (
        max(len(line) for line in actual.splitlines() + [""] if all(c == delimiter for c in line))
        + 1
    )
    expected.delimiter = delimiter * max(delimiter_length, 3)


@dataclasses.dataclass
class ReplTestMetadata:
    args: list[str] | None
    should_fail: bool | None
    files: list[str] | None

    content: str
    unknown: dict[str, str]
    file: Path

    @classmethod
    def keys_in_file(cls) -> set[str]:
        return ["args", "should_fail", "files"]

    @property
    def as_frontmatter(self) -> str:
        meta = dataclasses.asdict(self)
        file_items = {k: meta[k] for k in self.keys_in_file()}
        data = [f"{k}: {v}" for k, v in file_items.items() if v is not None]
        return "\n".join(["---", *data, "---", "\n"]) if data else ""

    def create_test(self) -> "ReplTest":
        exceptions = []

        if self.unknown:
            exceptions.append(
                ValueError(
                    f"Found unknown metadata: {self.unknown}\nValid metadata attributes are: {ReplTestMetadata.keys_in_file}"
                )
            )

        startup_block: ReplTestBlock | None = None
        blocks = []
        doc = mistletoe.Document(self.content)
        current_input: CodeFence | None = None
        for elem in list(doc.children):
            elem: BlockToken
            if not isinstance(elem, CodeFence):
                continue

            match elem.language:
                case "nix":
                    if current_input:
                        blocks.append(
                            ReplTestBlock(
                                current_input.content, _add_output_codefence(current_input)
                            )
                        )
                    current_input = elem
                case "output":
                    if current_input:
                        blocks.append(ReplTestBlock(current_input.content, elem))
                        current_input = None
                    elif not blocks and startup_block is None:
                        startup_block = ReplTestBlock("\n", elem)
                    else:
                        exceptions.append(
                            ValueError(
                                f"Found output block without input block at line {elem.line_number}"
                            )
                        )

        if current_input:
            blocks.append(
                ReplTestBlock(current_input.content, _add_output_codefence(current_input))
            )

        if not blocks and startup_block is None:
            exceptions.append(ValueError("not test input (or output) found"))

        if exceptions:
            raise ExceptionGroup("Invalid Test configuration:", exceptions)
        return ReplTest(startup_block, blocks, self, doc)


@dataclass
class ReplTestBlock:
    input: str
    output: CodeFence

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, str):
            return False
        return self.output.content.strip() == other.strip()


@dataclass
class ReplTest:
    startup_block: ReplTestBlock | None
    blocks: list[ReplTestBlock]
    metadata: ReplTestMetadata
    doc: mistletoe.Document

    @property
    def input(self) -> str:
        return "\n".join(block.input.strip() for block in self.blocks)

    def check_and_update(self, output: str, do_update: bool) -> bool:
        updated = False
        actual, blocks = self._output_to_blocks(output)
        always_start_block = self.startup_block or _create_startup_block()

        if not do_update:
            assert always_start_block == actual
        else:
            if always_start_block != actual:
                if self.startup_block is None:
                    self.doc.children.insert(0, always_start_block.output)
                _update_codefence(always_start_block, actual)
                updated = True

        for actual, expected in blocks:
            if not do_update:
                assert expected == actual
            else:
                if expected == actual:
                    continue
                updated = True
                _update_codefence(expected, actual)
        return updated

    def _output_to_blocks(self, output: str) -> tuple[str, list[tuple[str, ReplTestBlock]]]:
        output = output.split("\x05")
        start, *output = output
        if self.metadata.should_fail:
            return (start, [])

        blocks = []
        for block in self.blocks:
            tasks = block.input.count("\n")
            test_output, output = output[:tasks], output[tasks:]
            test_output = "".join(test_output)
            test_output = re.sub(r"^\s+$", "\n", test_output, flags=re.MULTILINE)
            blocks.append(("".join(test_output), block))

        return (start, blocks)


def _collect_repl_tests() -> list[Path]:
    return [
        file
        for test_folder in (functional2_base_folder / "repl_characterization").iterdir()
        if test_folder.is_dir()
        for file in test_folder.iterdir()
        if file.suffix == ".md"
    ]


def get_repl_test_params() -> tuple[list[tuple[FileDeclaration, ReplTestMetadata]]]:
    params = []
    ids = []
    for file in _collect_repl_tests():
        fm = frontmatter.loads(file.read_text())
        metadata = fm.metadata
        should_fail = metadata.pop("should_fail", None)
        args = metadata.pop("args", None)
        files = metadata.pop("files", None)
        files_param = {f: CopyFile(file.parent / f) for f in files or {}}
        params.append(
            (files_param, ReplTestMetadata(args, should_fail, files, fm.content, metadata, file))
        )
        ids.append(f"{file.parent.name}:{file.name}")
    return params, ids


def pytest_assertrepr_compare(config: Config, op: str, left: Any, right: Any) -> list[str] | None:
    if not isinstance(left, ReplTestBlock) or op != "==" or not isinstance(right, str):
        return None
    left: ReplTestBlock
    right: str

    exp_lines = left.output.content.strip().splitlines()
    act_lines = right.strip().splitlines()
    expl = [
        f"repl output of {left.input!r} differs. Consider using `--accept-tests` to update the golden files."
    ]
    if config.get_verbosity():
        expl.append("The following lines were mismatched:")
        l_exp = len(exp_lines)
        l_act = len(act_lines)
        for i in range(max(l_exp, l_act)):
            exp = exp_lines[i] if i < l_exp else None
            act = act_lines[i] if i < l_act else None
            if i > l_exp - 1:
                expl.append(f"{i + 1}: + {act}")
                continue
            if i > l_act - 1:
                expl.append(f"{i + 1}: - {exp}")
                continue
            if exp != act:
                expl.append(f"{i + 1}: - {exp}")
                expl.append(f"{i + 1}: + {act}")

    return expl
