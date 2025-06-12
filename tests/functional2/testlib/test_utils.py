from collections.abc import Callable, Generator
from typing import Literal, Any

import pytest
from functional2.testlib.utils import is_value_of_type


def test_list_type_valid():
    assert is_value_of_type([1, 2, 3], list[int])


def test_list_type_valid_multi_type():
    assert is_value_of_type([1, "a", 2], list[int | str])


def test_list_type_invalid_single_fail():
    assert not is_value_of_type([1, 2, "a"], list[int])


def test_list_type_invalid_all_wrong():
    assert not is_value_of_type([1, 2, 3], list[str])


def test_list_type_nested():
    assert is_value_of_type([[1], [2, 3], [4]], list[list[int]])


def test_list_type_nested_single_invalid():
    assert not is_value_of_type([[1], [2, 3], ["a"]], list[list[int]])


def test_weird_type_valid():
    assert is_value_of_type(42, Literal[42])


def test_type_type():
    class Foo: ...

    assert is_value_of_type(Foo, type[Foo])


def test_type_doesnt_allow_generator():
    def foo():
        yield 1

    with pytest.raises(ValueError, match=r"Unsupported expected_type.+"):
        is_value_of_type(foo, Generator[int, None, None])


def test_type_doesnt_allow_callable():
    def foo(a):  # noqa: ANN001, ANN202 # dummy stuff within testing, nothing external
        return "a" + str(a)

    with pytest.raises(ValueError, match=r"Unsupported expected_type.+"):
        is_value_of_type(foo, Callable[[int], str])


def test_type_allows_none():
    assert is_value_of_type(None, type[None])


def test_doesnt_allow_generic():
    class X[T]: ...

    with pytest.raises(ValueError, match=r"Unsupported expected_type.+"):
        is_value_of_type(0, X[int])


def test_type_dict_checks_valid():
    d = {"a": 1, "b": 2}
    assert is_value_of_type(d, dict[str, int])


def test_type_dict_key_invalid():
    d = {"a": 1, 1: 2}
    assert not is_value_of_type(d, dict[str, int])


def test_type_dict_value_invalid():
    d = {"a": 1, "b": "2"}
    assert not is_value_of_type(d, dict[str, int])


def test_type_allows_any():
    assert is_value_of_type(1, Any)
    assert is_value_of_type("a", Any)
    assert is_value_of_type(lambda x: x, Any)
