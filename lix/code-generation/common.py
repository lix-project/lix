import argparse
import re
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


def get_experimental_features(
    base_path: str, human_names: list[str | None]
) -> dict[str | None, str]:
    experimental_feature_files = {
        f"{base_path}/{xp_name}.md" for xp_name in human_names if xp_name is not None
    }

    from build_extra_features import ExtraFeature  # noqa: PLC0415 # Avoid cyclic import

    experimental_features_data = load_data(list(experimental_feature_files), ExtraFeature)
    experimental_features: dict[str | None, str] = {
        xf.name: f"Xp::{xf.internal_name}" for _, xf in experimental_features_data
    }
    experimental_features[None] = "std::nullopt"
    return experimental_features


FIELD_RENAMES = {"type": "type_str", "content": "documentation"}


def load_data[T](defs: list[str], parse_function: type[T]) -> list[tuple[str, T]]:
    data = []
    for path in defs:
        try:
            datum = {
                # convert camelCase to snake_case
                re.sub(r"(?<=.)([A-Z])", lambda m: f"_{m.group(1).lower()}", k): v
                for k, v in frontmatter.load(path).to_dict().items()
            }

            for post_name, field_name in FIELD_RENAMES.items():
                if post_name in datum:
                    datum[field_name] = datum.pop(post_name)

            data.append((path, parse_function(**datum)))
        except Exception as e:
            e.add_note(f"in {path}")
            raise
    return data


def generate_file[T](
    path: str | None,
    data: list[T],
    sort_key_function: Callable[[T], str],
    generate_function: Callable[[T], str],
):
    if path is not None:
        with Path(path).open("w") as out:
            for path, datum in sorted(
                data, key=lambda path_and_datum: sort_key_function(path_and_datum[1])
            ):
                try:
                    text = generate_function(datum)
                    out.write(text)
                except Exception as e:
                    e.add_note(f"in {path}")
                    raise


def get_argument_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", help="Path of the header to generate")
    ap.add_argument("--docs", help="Path of the documentation file to generate")
    ap.add_argument("defs", help="Builtin definition files", nargs="+")

    return ap
