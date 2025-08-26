#ifndef MINISTRING_H
#define MINISTRING_H

#include <Python.h>

typedef struct {
    PyObject_HEAD
    char *utf8_data;        // UTF-8 encoded bytes
    Py_ssize_t utf8_size;   // size in bytes
    int32_t *offsets;       // offset table: codepoint -> byte index
    Py_ssize_t length;      // number of codepoints
    Py_hash_t hash;         // cached hash
} Utf8StringObject;

// Function declarations
PyObject *Utf8String_FromUTF8(const char *data, Py_ssize_t size);
int utf8_codepoint_count(const char *data, Py_ssize_t size);
void build_offset_table(Utf8StringObject *self);
int utf8_char_length(unsigned char first_byte);

// Type object
extern PyTypeObject Utf8StringType;

#endif // MINISTRING_H
