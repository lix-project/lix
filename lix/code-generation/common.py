from collections.abc import Callable
from pathlib import Path
from typing import Any

import frontmatter


def cxx_escape_character(c: str) -> str:
    if 0x20 <= ord(c) < 0x7F and c != '"' and c != "?" and c != "\\":
        return c
    if c == "\t":
        return r"\t"
    if c == "\n":
        return r"\n"
    if c == "\r":
        return r"\r"
    if c == '"':
        return r"\""
    if c == "?":
        return r"\?"
    if c == "\\":
        return r"\\"
    if ord(c) <= 0xFFFF:
        return str.format(r"\u{:04x}", ord(c))
    return str.format(r"\U{:08x}", ord(c))


def cxx_literal(v: Any) -> str:
    if v is None:
        return "std::nullopt"
    if v is False:
        return "false"
    if v is True:
        return "true"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return "".join(['"', *(cxx_escape_character(c) for c in v), '"'])
    if isinstance(v, list):
        return f"{{{', '.join([cxx_literal(item) for item in v])}}}"
    msg = f"cannot represent {v!r} in C++"
    raise NotImplementedError(msg)


def load_data[T](defs: list[str], parse_function: Callable[[frontmatter.Post], T]) -> list[T]:
    data = []
    for path in defs:
        try:
            datum = frontmatter.load(path)
            data.append((path, parse_function(datum)))
        except Exception as e:
            e.add_note(f"in {path}")
            raise
    return data


def generate_file(
    path: str | None,
    data: list[Any],
    sort_key_function: Callable[[Any], str],
    generate_function: Callable[[Any], str],
):
    if path is not None:
        with Path(path).open("w") as out:
            for path, datum in sorted(
                data, key=lambda path_and_datum: sort_key_function(path_and_datum[1])
            ):
                try:
                    out.write(generate_function(datum))
                except Exception as e:
                    e.add_note(f"in {path}")
                    raise
