import os
import unicodedata
from io import BytesIO
from logging import Logger
from pathlib import Path

import pytest

from ..testlib.fixtures.nix import Nix
from ..testlib.nar import (
    DirectoryUnordered,
    NarItem,
    NarListener,
    Regular,
    Symlink,
    write_with_export_header,
)

meow_orig = "méow"
meow_nfc_ = unicodedata.normalize("NFC", meow_orig)
meow_nfd_ = unicodedata.normalize("NFD", meow_orig)
meow_nfc = meow_nfc_.encode("utf-8")
meow_nfd = meow_nfd_.encode("utf-8")
assert meow_nfc != meow_nfd

EVIL_NARS: list[tuple[str, NarItem]] = [
    (
        "valid-dir-1",
        DirectoryUnordered(
            [
                (b"a-nested", DirectoryUnordered([(b"loopy", Symlink(b"../abc-nested"))])),
                (b"b-file", Regular(False, b"meow kbity")),
                (b"c-exe", Regular(True, b"#!/usr/bin/env cat\nmeow kbity")),
            ]
        ),
    ),
    (
        "invalid-slashes-1",
        DirectoryUnordered(
            [(b"meow", Symlink(b"meowmeow")), (b"meow/nya", Regular(False, b"eepy"))]
        ),
    ),
    ("invalid-dot-1", DirectoryUnordered([(b".", Symlink(b"meowmeow"))])),
    ("invalid-dot-2", DirectoryUnordered([(b"..", Symlink(b"meowmeow"))])),
    ("invalid-nul-1", DirectoryUnordered([(b"meow\0nya", Symlink(b"meowmeow"))])),
    (
        "invalid-misorder-1",
        DirectoryUnordered(
            [(b"zzz", Regular(False, b"eepy")), (b"kbity", Regular(False, b"meow"))]
        ),
    ),
    (
        "invalid-dupe-1",
        DirectoryUnordered([(b"zzz", Regular(False, b"eepy")), (b"zzz", Regular(False, b"meow"))]),
    ),
    (
        "invalid-dupe-2",
        DirectoryUnordered(
            [
                (b"zzz", DirectoryUnordered([(b"meow", Regular(False, b"kbity"))])),
                (b"zzz", Regular(False, b"meow")),
            ]
        ),
    ),
    (
        "invalid-dupe-3",
        DirectoryUnordered(
            [
                (b"zzz", DirectoryUnordered([(b"meow", Regular(False, b"kbity"))])),
                (b"zzz", DirectoryUnordered([(b"meow", Regular(False, b"kbityy"))])),
            ]
        ),
    ),
    (
        "invalid-dupe-4",
        DirectoryUnordered(
            [
                (b"zzz", Symlink(b"../kbity")),
                (b"zzz", DirectoryUnordered([(b"meow", Regular(False, b"kbityy"))])),
            ]
        ),
    ),
    (
        "invalid-casehack-1",
        DirectoryUnordered(
            [
                (b"ZZZ~nix~case~hack~2", Regular(False, b"meow")),
                (b"zzz~nix~case~hack~1", Regular(False, b"eepy")),
            ]
        ),
    ),
    (
        "invalid-casehack-2",
        DirectoryUnordered(
            [
                (b"ZZZ~nix~case~hack~1", Regular(False, b"meow")),
                (b"zzz~nix~case~hack~1", Regular(False, b"eepy")),
            ]
        ),
    ),
]


@pytest.mark.parametrize(("name", "nar"), EVIL_NARS, ids=next(zip(*EVIL_NARS)))
def test_evil_nar(nix: Nix, name: str, nar: NarItem, logger: Logger):
    bio = BytesIO()

    listener = NarListener(bio)
    write_with_export_header(nar, name.encode(), listener)
    logger.info(nar)

    if name.startswith("valid-"):
        expected_rc = 0
    elif name.startswith("invalid-"):
        expected_rc = 1
    else:
        raise ValueError("bad name", name)

    res = nix.nix_store(["--import"]).with_stdin(bio.getvalue()).run().expect(expected_rc)
    logger.info(res)


def test_unicode_evil_nar(nix: Nix, tmp_path: Path, logger: Logger):
    """
    Depending on the filesystem in use, filenames that are equal modulo unicode
    normalization may hit the same file or not.

    On macOS, such collisions will result in hitting the same file. We detect
    if the fs is like this before checking what Lix does.
    """
    with open(os.path.join(bytes(tmp_path), meow_nfc), "wb") as fh:
        fh.write(b"meow")

    try:
        with open(os.path.join(bytes(tmp_path), meow_nfd), "rb") as fh:
            assert fh.read() == b"meow"
    except FileNotFoundError:
        # normalization is not applied to this system
        pytest.skip("filesystem does not use unicode normalization")

    test_evil_nar(
        nix,
        "invalid-unicode-normalization-1",
        DirectoryUnordered(
            [
                # méow
                (meow_nfd, Regular(False, b"eepy")),
                (meow_nfc, Symlink(b"meowmeow")),
            ]
        ),
        logger,
    )
    test_evil_nar(
        nix,
        "invalid-unicode-normalization-2",
        DirectoryUnordered(
            [
                # méow
                (meow_nfd, Symlink(b"meowmeow")),
                (meow_nfc, Regular(False, b"eepy")),
            ]
        ),
        logger,
    )
