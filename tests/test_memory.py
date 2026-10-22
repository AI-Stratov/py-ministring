import sys

import pytest
from ministring import ministr

NON_ASCII = {
    "emoji": "a" * 1000 + "\U0001F600",
    "cyrillic": "я" * 1000,
    "latin1": "café" * 250,
    "euro": "€" + "a" * 999,
}


def fresh(text):
    """An equal str that is a different object, with no cached UTF-8."""
    return (text + ".")[:-1]


@pytest.mark.parametrize("text", NON_ASCII.values(), ids=NON_ASCII.keys())
def test_constructor_does_not_grow_source_str(text):
    # PyUnicode_AsUTF8AndSize() caches a UTF-8 copy inside the str for the
    # rest of its life, and str.__sizeof__ counts it.
    text = fresh(text)
    before = sys.getsizeof(text)
    ministr(text)
    assert sys.getsizeof(text) == before


@pytest.mark.parametrize("text", NON_ASCII.values(), ids=NON_ASCII.keys())
def test_comparison_does_not_grow_other_str(text):
    s = ministr(text)
    other = fresh(text)
    before = sys.getsizeof(other)
    assert s == other
    assert other == s
    assert not (s != other)
    assert sys.getsizeof(other) == before


def test_sizeof_counts_utf8_buffer():
    empty = sys.getsizeof(ministr(""))
    text = "a" * 1000 + "\U0001F600"
    assert sys.getsizeof(ministr(text)) >= empty + len(text.encode())


def test_iteration_does_not_build_index():
    s = ministr("я" * 1000)
    before = sys.getsizeof(s)
    assert "".join(s) == "я" * 1000
    assert sys.getsizeof(s) == before
