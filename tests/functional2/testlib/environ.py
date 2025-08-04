"""
Safe (read-only) environment access. This exists entirely so that we can ban
os.environ in our codebase.
"""

import collections.abc
import os
from typing import TypeVar, overload
from collections.abc import Iterator

_T = TypeVar("_T")


class ReadonlyDict(collections.abc.Mapping[str, str]):
    def __init__(self, inner: os._Environ[str]):
        self.inner = inner

    def __getitem__(self, key: str) -> str:
        return self.inner[key]

    def __iter__(self) -> Iterator[str]:
        return iter(self.inner)

    def __len__(self) -> int:
        return len(self.inner)

    @overload
    def get(self, key: str, /) -> str | None: ...
    @overload
    def get(self, key: str, /, default: str | _T) -> str | _T: ...

    def get(self, key: str, /, default: str | _T = None) -> str | _T:
        return self.inner.get(key, default=default)

    def copy(self) -> dict[str, str]:
        return self.inner.copy()


# SAFETY: wrapped to not allow mutation
environ = ReadonlyDict(os.environ)  # noqa: TID251
