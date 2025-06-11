"""
See "The Purely Functional Software Deployment Model", fig. 5.2 [1].

[1]: E. Dolstra, “The purely functional software deployment model,” Ph.D., Universeit Utrecht, Utrecht, NL, 2006. [Online]. Available: https://edolstra.github.io/pubs/phd-thesis.pdf
"""

from abc import ABCMeta, abstractmethod
import dataclasses
import struct
from typing import Protocol, ClassVar


class Writable(Protocol):
    """Realistically could just be IOBase but this is more constrained"""

    def write(self, data: bytes, /) -> int: ...


@dataclasses.dataclass
class NarListener:
    data: Writable

    def literal(self, data: bytes):
        self.data.write(data)

    def int_(self, v: int):
        self.literal(struct.pack("<Q", v))

    def add_pad(self, data_len: int):
        npad = 8 - data_len % 8
        if npad == 8:
            npad = 0
        # FIXME(Jade): implement nonzero padding
        self.literal(b"\0" * npad)

    def str_(self, data: bytes):
        self.int_(len(data))
        self.literal(data)
        self.add_pad(len(data))


class NarItem(metaclass=ABCMeta):
    type_: bytes

    def serialize(self, out: NarListener):
        out.str_(b"type")
        out.str_(self.type_)
        self.serialize_type(out)

    @abstractmethod
    def serialize_type(self, out: NarListener):
        pass


@dataclasses.dataclass
class Regular(NarItem):
    executable: bool
    contents: bytes
    type_: ClassVar[bytes] = b"regular"

    def serialize_type(self, out: NarListener):
        if self.executable:
            out.str_(b"executable")
            out.str_(b"")
        out.str_(b"contents")
        out.str_(self.contents)


@dataclasses.dataclass
class DirectoryUnordered(NarItem):
    entries: list[tuple[bytes, NarItem]]
    """Entries in the directory, not required to be in order because this nar is evil"""
    type_: ClassVar[bytes] = b"directory"

    @staticmethod
    def entry(out: NarListener, name: bytes, item: "NarItem"):
        # lol this format
        out.str_(b"entry")
        out.str_(b"(")
        out.str_(b"name")
        out.str_(name)
        out.str_(b"node")
        out.str_(b"(")
        item.serialize(out)
        out.str_(b")")
        out.str_(b")")

    def serialize_type(self, out: NarListener):
        for name, entry in self.entries:
            self.entry(out, name, entry)


@dataclasses.dataclass
class Directory(NarItem):
    entries: dict[bytes, NarItem]

    def serialize_type(self, out: NarListener):
        for name, item in sorted(self.entries.items(), key=lambda v: v[0]):
            DirectoryUnordered.entry(out, name, item)


@dataclasses.dataclass
class Symlink(NarItem):
    target: bytes
    type_: ClassVar[bytes] = b"symlink"

    def serialize_type(self, out: NarListener):
        out.str_(b"target")
        out.str_(self.target)


def serialize_nar(toplevel: NarItem, out: NarListener):
    out.str_(b"nix-archive-1")
    out.str_(b"(")
    toplevel.serialize(out)
    out.str_(b")")


def write_with_export_header(nar: NarItem, name: bytes, out: NarListener):
    # n.b. this is *not* actually a nar serialization, it just happens that nix
    # used exactly the same format for ints and strings in its protocol (and
    # nix-store --export) as it did in NARs lol
    export_magic = 0x4558494E

    # Store::exportPaths
    # For each path, put 1 then exportPath
    out.int_(1)

    # Store::exportPath
    serialize_nar(nar, out)
    out.int_(export_magic)
    # Due to `nix` setting the store using chroots /nix/store is still the test-local store and not the global one
    store_path = b"/nix/store"
    out.str_(store_path + b"/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-" + name)
    # no references
    out.int_(0)
    # no deriver
    out.str_(b"")
    # end of path
    out.int_(0)

    out.int_(0)
