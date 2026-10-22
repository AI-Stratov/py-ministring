#ifndef MINISTRING_H
#define MINISTRING_H

#include <Python.h>

// The index stores the byte offset of every MINISTR_INDEX_STRIDE-th
// codepoint; any other codepoint is at most MINISTR_INDEX_STRIDE - 1
// characters past an indexed one.
#define MINISTR_INDEX_STRIDE 64

typedef struct {
    PyObject_HEAD
    char *utf8_data;        // UTF-8 encoded bytes
    Py_ssize_t utf8_size;   // size in bytes
    Py_ssize_t *index;      // sparse index: codepoint / MINISTR_INDEX_STRIDE -> byte offset;
                            // built on first indexing, never for ASCII
    Py_ssize_t length;      // number of codepoints
    Py_hash_t hash;         // cached hash
} Utf8StringObject;

// Function declarations

// data must be valid UTF-8; size < 0 means data is NUL-terminated
PyObject *Utf8String_FromUTF8(const char *data, Py_ssize_t size);

// Type object
extern PyTypeObject Utf8StringType;

#endif // MINISTRING_H
