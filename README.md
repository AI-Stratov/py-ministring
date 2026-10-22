# py-ministring

An experimental UTF-8 string type for CPython, written as a C extension.
It is a learning project about how CPython stores strings, not a
replacement for `str`.

## The idea

CPython's `str` ([PEP 393](https://peps.python.org/pep-0393/)) stores every
character with the width of the widest one in the string: 1 byte if all
characters are below U+0100, 2 bytes if any is above that, 4 bytes if any is
outside the Basic Multilingual Plane. A megabyte of ASCII with a single emoji
therefore takes 4 MB as a `str`.

`ministr` stores the text as UTF-8 instead, where each character takes only
as many bytes as it needs.

## Memory

Bytes per character for 1,000,000 characters (Python 3.13, 64-bit Linux),
produced by [`benchmarks/memory.py`](benchmarks/memory.py):

| Text | `str` | `ministr` | `ministr` after `s[0]` |
|---|---|---|---|
| ASCII | 1.00 | 1.00 | 1.00 |
| ASCII + one emoji | 4.00 | 1.00 | 1.13 |
| ASCII + one € | 2.00 | 1.00 | 1.13 |
| Latin-1 (café) | 1.00 | 1.25 | 1.38 |
| Cyrillic | 2.00 | 2.00 | 2.13 |
| CJK | 2.00 | 3.00 | 3.13 |

So `ministr` only wins on text that is mostly narrow characters with a few
wide ones. On pure ASCII it ties with `str`. On Latin-1, Cyrillic or CJK
text, UTF-8 is as large as or larger than CPython's fixed-width storage.
Short strings cost more too: the object header is 56 bytes, against 40 for
an ASCII `str`.

## Indexing

UTF-8 has variable-width characters, so finding character `i` needs to know
where it starts:

- **ASCII strings** never need an index: character `i` is byte `i`.
- **Other strings** build a sparse index on the first `s[i]` or `s[a:b]`. It
  stores the byte offset of every 64th character, 8 bytes per 64 characters
  or 0.125 bytes per character. A lookup jumps to the nearest entry and
  scans forward at most 63 characters, so access is O(1) with a bounded
  constant.
- **Iteration** (`for ch in s`, `list(s)`, `"".join(s)`) walks the bytes and
  never builds the index.

The sparse index makes random access slower. On mixed-width text a random
`s[i]` takes about 165 ns, against about 30 ns for `str`. A full table
(one offset per character, as in 0.1.x) gave about 60 ns but cost 4 bytes
per character. `MINISTR_INDEX_STRIDE` in `src/ministring.h` sets the
trade-off: a stride of 32 gives about 110 ns at 0.25 bytes per character.

## Installation

```bash
git clone https://github.com/AI-Stratov/py-ministring
cd py-ministring
pip install .
```

## Usage

```python
from ministring import ministr

s = ministr("hello 😃 world")

len(s)          # 13
s[6]            # '😃' (a regular str)
s[-1]           # 'd'
s[0:5]          # 'hello' (a new ministr)
str(s[8:])      # 'world'
list(s[4:7])    # ['o', ' ', '😃']

"lo" in s       # True
s == "hello 😃 world"   # True, works both ways and with other ministr

d = {s: "value"}
d["hello 😃 world"]     # 'value': hash(s) == hash(str(s))
```

## API

- `ministr(obj="")` creates a `ministring.Utf8String` from a `str`, or from
  `str(obj)` for anything else. Strings with lone surrogates have no UTF-8
  form and raise `UnicodeEncodeError`.
- `len(s)` is the number of codepoints.
- `s[i]` returns the character as a `str`. Negative indices work.
- `s[a:b]` returns a new `Utf8String`. Steps other than 1 raise
  `NotImplementedError`.
- `iter(s)` yields characters as `str`.
- `x in s` searches for a substring. `x` can be a `str` or a `Utf8String`.
- `s == x` and `s != x` compare with a `str` or a `Utf8String`.
- `hash(s)` equals `hash(str(s))`, so `ministr` and `str` keys find each
  other in dicts and sets. It is computed once and cached.
- `str(s)` and `repr(s)` behave like those of the equivalent `str`.
- `sys.getsizeof(s)` includes the UTF-8 buffer and, once built, the index.

## Limitations

This is a prototype:

- It is not a `str` subclass. Most of the standard library wants a real
  `str`, so pass `str(s)`.
- There are no string methods (`find`, `split`, `upper`, ...), no
  concatenation, no ordering comparisons and no pickling.
- `str(s)` decodes the whole string every time.
- Random access on non-ASCII text is about 5 times slower than `str` (see
  [Indexing](#indexing)).
- `in` is a plain byte search, O(n·m) in the worst case.
- CI covers Python 3.9 to 3.14.

## Tests

```bash
pip install . pytest
python -X dev -m pytest
```

`-X dev` turns on CPython's debug allocator, which fills new memory with
`0xCD`, so code that reads an uninitialized field crashes instead of passing
by luck. `tests/test_oom.py` uses `_testcapi.set_nomemory()` to make each
allocation fail in turn and checks that every failure becomes a
`MemoryError`.

CI also builds the extension with AddressSanitizer and
UndefinedBehaviorSanitizer. To do the same locally on Linux:

```bash
CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g" \
LDFLAGS="-fsanitize=address,undefined" pip install . pytest

LD_PRELOAD="$(gcc -print-file-name=libasan.so)" PYTHONMALLOC=malloc \
ASAN_OPTIONS=detect_leaks=0 python -m pytest
```

The interpreter is not instrumented, so the ASan runtime is preloaded.
`PYTHONMALLOC=malloc` sends every allocation through ASan instead of
pymalloc.

## Data structure

```c
typedef struct {
    PyObject_HEAD
    char *utf8_data;        // UTF-8 encoded bytes
    Py_ssize_t utf8_size;   // size in bytes
    Py_ssize_t *index;      // sparse index: codepoint / 64 -> byte offset;
                            // built on first indexing, never for ASCII
    Py_ssize_t length;      // number of codepoints
    Py_hash_t hash;         // cached hash
} Utf8StringObject;
```

## Changes since 0.1.1

- The first `s[i]` used to build a 4-byte offset for every character, which
  made indexed strings 4 bytes per character larger than the matching
  `str`. That table is now the sparse index described above.
- `ministr(text)` and `ministr == text` used to call
  `PyUnicode_AsUTF8AndSize()`. That call caches a UTF-8 copy inside `text`
  for as long as `text` lives, which doubled the real cost of a `ministr`
  for non-ASCII text.
- If allocating the UTF-8 buffer failed, the destructor freed an
  uninitialized pointer. ASan and `-X dev` both catch it.
- Iteration no longer builds an index.
- `in` searches for substrings. It used to compare the needle with each
  single character.
- `sys.getsizeof()` reports the buffers a `ministr` owns.

## License

MIT, see [LICENCE](LICENCE).
