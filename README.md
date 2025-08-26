# py-ministring

Experimental compact UTF-8 string type for CPython as a C-extension.

## Description

py-ministring implements a new string-like type `Utf8String` with efficient Unicode indexing and slicing. This prototype is designed to reduce memory footprint when working with texts containing predominantly ASCII characters with occasional multi-byte characters (like emojis).

## Why py-ministring?

- **Compact Storage**: Stores original UTF-8 bytes instead of wide characters
- **O(1) Indexing**: Uses offset table for fast character access
- **Hash Caching**: Speeds up comparison operations and dictionary usage
- **Compatibility**: Full compatibility with regular Python strings

## Installation

```bash
git clone https://github.com/AI-Stratov/py-ministring
cd py-ministring
python setup.py build_ext --inplace
```

## Usage

```python
from ministring import ministr

# Create a string
s = ministr("hello 😃 world")

# Length in codepoints
print(len(s))      # 13

# Indexing
print(s[6])        # "😃"
print(s[0])        # "h"
print(s[-1])       # "d"

# Slicing
print(str(s[0:5]))    # "hello"
print(str(s[6:7]))    # "😃"
print(str(s[8:]))     # "world"

# Convert to regular string
print(str(s))      # "hello 😃 world"

# Comparison
assert s == "hello 😃 world"
assert "hello 😃 world" == s

# Hashing (can use in dict/set)
d = {s: "value"}
s2 = ministr("hello 😃 world")
print(d[s2])       # "value"
```

## API

### Constructor

- `ministr(obj)` - creates a new Utf8String object from a string or str()-convertible object

### Methods

- `len(s)` - returns the number of Unicode codepoints
- `s[i]` - returns character at index as a regular Python string
- `s[start:stop]` - returns a new Utf8String with slice
- `str(s)` - converts to regular Python string
- `repr(s)` - string representation for debugging
- `hash(s)` - hash value (cached)
- `s == other` - comparison with other Utf8String or regular strings

## Data Structure

```c
typedef struct {
    PyObject_HEAD
    char *utf8_data;        // UTF-8 bytes
    Py_ssize_t utf8_size;   // size in bytes
    int32_t *offsets;       // offset table: codepoint → byte
    Py_ssize_t length;      // number of codepoints
    Py_hash_t hash;         // cached hash
} Utf8StringObject;
```

## Testing

Run tests with pytest:

```bash
pip install pytest
pytest -v
```

## Limitations

⚠️ **WARNING**: This is an experimental prototype, not intended for production use!

- Missing support for many string methods (`find`, `replace`, etc.)
- May be slower than regular strings for some operations
- No support for step slicing (`s[::2]`)
- Limited handling of invalid UTF-8
- No optimizations for very long strings

## Technical Details

### C API

Core functions for working with Utf8String:

- `Utf8String_FromUTF8(data, size)` - create from UTF-8 data
- `utf8_codepoint_count(data, size)` - count codepoints
- `build_offset_table(self)` - build offset table
- `utf8_char_length(first_byte)` - determine UTF-8 character length

### Architecture

1. **Data Storage**: Original UTF-8 bytes are preserved unchanged
2. **Indexing**: Offset table built on-demand for O(1) access
3. **Caching**: Hash values cached for faster comparisons
4. **Compatibility**: Full support for Python protocols (sequence, mapping)

## Usage Examples

### Working with Emojis

```python
s = ministr("Hello 👋 world 🌍!")
print(f"Length: {len(s)}")           # Length: 14
print(f"Emojis: {s[6]}, {s[12]}")    # Emojis: 👋, 🌍
```

### Multi-language Text Processing

```python
s = ministr("Hello 世界 🌍 Мир")
print(f"English: {str(s[0:5])}")     # Hello
print(f"Chinese: {str(s[6:8])}")     # 世界
print(f"Emoji: {s[9]}")              # 🌍
print(f"Russian: {str(s[11:14])}")   # Мир
```

### Performance

```python
# Creating many strings with emojis
texts = [ministr(f"Text {i} 😀") for i in range(1000)]
text_set = set(texts)  # Fast thanks to cached hash
```

## License

Experimental code for educational purposes.
