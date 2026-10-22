import pytest
from ministring import ministr

def test_empty_string():
    """Test empty string creation and basic properties"""
    s = ministr("")
    assert len(s) == 0
    assert str(s) == ""

def test_basic_ascii():
    """Test basic ASCII string functionality"""
    s = ministr("hello")
    assert len(s) == 5
    assert str(s) == "hello"
    assert s[0] == "h"
    assert s[4] == "o"
    assert s[-1] == "o"

def test_unicode_emoji():
    """Test Unicode handling with emojis"""
    s = ministr("hello 😃 world")
    assert len(s) == 13  # 13 codepoints: h-e-l-l-o-space-😃-space-w-o-r-l-d
    assert str(s) == "hello 😃 world"
    assert s[6] == "😃"
    assert s[0] == "h"
    assert s[-1] == "d"

def test_slicing():
    """Test string slicing functionality"""
    s = ministr("hello 😃 world")
    assert str(s[0:5]) == "hello"
    assert str(s[6:7]) == "😃"
    assert str(s[8:13]) == "world"
    assert str(s[:5]) == "hello"
    assert str(s[8:]) == "world"
    assert str(s[:]) == "hello 😃 world"

def test_negative_indexing():
    """Test negative indexing"""
    s = ministr("abc😃def")
    assert s[-1] == "f"
    assert s[-2] == "e"
    assert s[-4] == "😃"
    assert str(s[-3:]) == "def"

def test_equality_with_str():
    """Test equality comparison with regular Python strings"""
    s = ministr("abc")
    assert s == "abc"
    assert "abc" == s
    assert not (s == "def")
    assert not ("def" == s)
    assert s != "def"
    assert "def" != s

def test_equality_with_ministr():
    """Test equality comparison between ministr objects"""
    s1 = ministr("hello")
    s2 = ministr("hello")
    s3 = ministr("world")

    assert s1 == s2
    assert s2 == s1
    assert not (s1 == s3)
    assert s1 != s3

def test_hash():
    """Test hash functionality"""
    s1 = ministr("hello")
    s2 = ministr("hello")
    s3 = ministr("world")

    assert hash(s1) == hash(s2)
    assert hash(s1) != hash(s3)

    # Should be able to use in dict/set
    d = {s1: "value"}
    assert d[s2] == "value"

def test_repr():
    """Test string representation"""
    s = ministr("hello")
    assert repr(s) == "'hello'"

    s_unicode = ministr("hello 😃")
    assert "😃" in repr(s_unicode)

def test_index_errors():
    """Test index out of range errors"""
    s = ministr("abc")

    with pytest.raises(IndexError):
        _ = s[3]

    with pytest.raises(IndexError):
        _ = s[-4]

def test_mixed_unicode():
    """Test mixed ASCII and Unicode characters"""
    s = ministr("café🌟")
    assert len(s) == 5  # c, a, f, é, 🌟
    assert s[3] == "é"
    assert s[4] == "🌟"
    assert str(s[0:4]) == "café"

def test_constructor_with_non_string():
    """Test constructor with non-string objects"""
    s = ministr(123)
    assert str(s) == "123"

    s = ministr(None)
    assert str(s) == "None"

def test_empty_slice():
    """Test empty slicing results"""
    s = ministr("hello")
    assert str(s[2:2]) == ""
    assert str(s[5:10]) == ""

def test_large_unicode():
    """Test with various Unicode characters"""
    s = ministr("Hello 世界 🌍 Мир")
    assert len(s) == 14  # Count codepoints: H-e-l-l-o-space-世-界-space-🌍-space-М-и-р
    assert "世" in str(s)
    assert "🌍" in str(s)
    assert "М" in str(s)

def test_equality_with_non_ascii_str():
    """Equality with str compares codepoints, not just lengths"""
    s = ministr("café😃")
    assert s == "café😃"
    assert s != "cafe😃"
    assert s != "café😄"
    assert s != "café"
    assert ministr("abé") != "abc"
    assert ministr("abc") != "abé"

def test_equality_with_lone_surrogate():
    """A str with a lone surrogate has no UTF-8 form and is simply not equal"""
    assert ministr("a") != "\ud800"
    assert not (ministr("a") == "\ud800")

def test_iteration():
    """Iteration yields one-character strings, like str"""
    text = "hello 😃 wörld"
    assert list(ministr(text)) == list(text)
    assert list(ministr("")) == []

    it = iter(ministr("ab"))
    assert iter(it) is it
    assert next(it) == "a"
    assert next(it) == "b"
    with pytest.raises(StopIteration):
        next(it)
    with pytest.raises(StopIteration):
        next(it)

def test_contains_substring():
    """`in` searches for substrings, like str"""
    s = ministr("hello 😃 world")
    assert "lo" in s
    assert "😃 w" in s
    assert "h" in s
    assert "" in s
    assert ministr("world") in s
    assert "x" not in s
    assert "world!" not in s
    assert "\ud800" not in s
    assert "" in ministr("")
    assert "a" not in ministr("")

def test_contains_requires_string():
    with pytest.raises(TypeError):
        1 in ministr("123")
