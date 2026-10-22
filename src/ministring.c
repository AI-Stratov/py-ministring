#include "ministring.h"
#include <string.h>

// Helper function to determine UTF-8 character length from first byte.
// A table lookup on the high nibble instead of a chain of ifs: byte_offset()
// calls this up to 63 times in a row, and on text that mixes character
// widths the branches mispredict. Data is always valid UTF-8 (it comes from
// CPython's encoder), so the table only needs to be right for lead bytes.
static int utf8_char_length(unsigned char first_byte) {
    static const unsigned char length_by_high_nibble[16] = {
        1, 1, 1, 1, 1, 1, 1, 1,  // 0xxxxxxx
        1, 1, 1, 1,              // 10xxxxxx, continuation byte, not a lead
        2, 2,                    // 110xxxxx
        3,                       // 1110xxxx
        4,                       // 11110xxx
    };
    return length_by_high_nibble[first_byte >> 4];
}

// Decode one UTF-8 character of char_len bytes into a codepoint
static Py_UCS4 utf8_decode(const unsigned char *p, int char_len) {
    switch (char_len) {
    case 1:
        return p[0];
    case 2:
        return ((Py_UCS4)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3:
        return ((Py_UCS4)(p[0] & 0x0F) << 12) | ((Py_UCS4)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    default:
        return ((Py_UCS4)(p[0] & 0x07) << 18) | ((Py_UCS4)(p[1] & 0x3F) << 12) |
               ((Py_UCS4)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
}

// Count UTF-8 codepoints in a byte string
static Py_ssize_t utf8_codepoint_count(const char *data, Py_ssize_t size) {
    Py_ssize_t count = 0;
    Py_ssize_t i = 0;

    while (i < size) {
        int char_len = utf8_char_length((unsigned char)data[i]);
        count++;
        i += char_len;
        if (i > size) break; // Prevent overrun on invalid UTF-8
    }

    return count;
}

// UTF-8 bytes of a str. PyUnicode_AsUTF8AndSize() is not used for non-ASCII
// strings: it caches a UTF-8 copy inside the str for the rest of its life.
// Instead the str is encoded into a temporary bytes object, returned in
// *owner (NULL for ASCII), which the caller releases with Py_XDECREF once
// done with the data.
static const char *unicode_as_utf8(PyObject *str, Py_ssize_t *size, PyObject **owner) {
    *owner = NULL;
#if PY_VERSION_HEX < 0x030C0000
    if (PyUnicode_READY(str) < 0) {
        return NULL;
    }
#endif
    if (PyUnicode_IS_ASCII(str)) {
        // An ASCII str stores one byte per character, which is already UTF-8
        *size = PyUnicode_GET_LENGTH(str);
        return PyUnicode_DATA(str);
    }

    PyObject *bytes = PyUnicode_AsUTF8String(str);
    if (!bytes) return NULL;

    *owner = bytes;
    *size = PyBytes_GET_SIZE(bytes);
    return PyBytes_AS_STRING(bytes);
}

// Whether needle occurs in haystack, as bytes. That is enough for UTF-8:
// a lead byte never looks like a continuation byte, so a match of a valid
// needle can only start on a character boundary.
static int bytes_contain(const char *haystack, Py_ssize_t haystack_size,
                         const char *needle, Py_ssize_t needle_size) {
    if (needle_size == 0) return 1;
    if (needle_size > haystack_size) return 0;

    const char *p = haystack;
    const char *last = haystack + (haystack_size - needle_size);
    while (p <= last) {
        p = memchr(p, needle[0], last - p + 1);
        if (!p) return 0;
        if (memcmp(p, needle, needle_size) == 0) return 1;
        p++;
    }
    return 0;
}

// Number of entries in the sparse index
static Py_ssize_t index_entries(Utf8StringObject *self) {
    return (self->length + MINISTR_INDEX_STRIDE - 1) / MINISTR_INDEX_STRIDE;
}

// Build the sparse index: entry k is the byte offset of codepoint
// k * MINISTR_INDEX_STRIDE. That is one Py_ssize_t per 64 codepoints
// (0.125 bytes per codepoint on 64-bit) instead of 4 bytes per codepoint.
static int build_index(Utf8StringObject *self) {
    Py_ssize_t *index = PyMem_New(Py_ssize_t, index_entries(self));
    if (!index) {
        PyErr_NoMemory();
        return -1;
    }

    Py_ssize_t byte_pos = 0;
    for (Py_ssize_t cp = 0; cp < self->length; cp++) {
        if (cp % MINISTR_INDEX_STRIDE == 0) {
            index[cp / MINISTR_INDEX_STRIDE] = byte_pos;
        }
        byte_pos += utf8_char_length((unsigned char)self->utf8_data[byte_pos]);
    }

    self->index = index;
    return 0;
}

// Byte offset of codepoint cp, for 0 <= cp <= length. Jumps to the nearest
// index entry at or before cp and scans forward at most
// MINISTR_INDEX_STRIDE - 1 characters. Returns -1 with an exception set if
// the index could not be built.
static Py_ssize_t byte_offset(Utf8StringObject *self, Py_ssize_t cp) {
    if (self->utf8_size == self->length) {
        return cp; // ASCII: one byte per codepoint, no index needed
    }
    if (cp == self->length) {
        return self->utf8_size;
    }
    if (!self->index && build_index(self) < 0) {
        return -1;
    }

    Py_ssize_t byte_pos = self->index[cp / MINISTR_INDEX_STRIDE];
    for (Py_ssize_t i = cp % MINISTR_INDEX_STRIDE; i > 0; i--) {
        byte_pos += utf8_char_length((unsigned char)self->utf8_data[byte_pos]);
    }
    return byte_pos;
}

// Deallocation function
static void Utf8String_dealloc(Utf8StringObject *self) {
    if (self->utf8_data) {
        PyMem_Free(self->utf8_data);
    }
    if (self->index) {
        PyMem_Free(self->index);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

// __len__ method
static Py_ssize_t Utf8String_length(Utf8StringObject *self) {
    return self->length;
}

// __getitem__ method
static PyObject *Utf8String_getitem(Utf8StringObject *self, Py_ssize_t index) {
    if (index < 0) {
        index += self->length;
    }

    if (index < 0 || index >= self->length) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return NULL;
    }

    Py_ssize_t start_byte = byte_offset(self, index);
    if (start_byte < 0) return NULL;

    int char_len = utf8_char_length((unsigned char)self->utf8_data[start_byte]);
    return PyUnicode_FromStringAndSize(self->utf8_data + start_byte, char_len);
}

// Slicing support
static PyObject *Utf8String_getslice(Utf8StringObject *self, Py_ssize_t start, Py_ssize_t stop) {
    if (start < 0) start = 0;
    if (stop > self->length) stop = self->length;
    if (start >= stop) {
        return Utf8String_FromUTF8("", 0);
    }

    Py_ssize_t start_byte = byte_offset(self, start);
    if (start_byte < 0) return NULL;
    Py_ssize_t end_byte = byte_offset(self, stop);
    if (end_byte < 0) return NULL;

    return Utf8String_FromUTF8(self->utf8_data + start_byte, end_byte - start_byte);
}

// Mapping protocol for indexing and slicing
static PyObject *Utf8String_subscript(Utf8StringObject *self, PyObject *key) {
    if (PyIndex_Check(key)) {
        Py_ssize_t index = PyNumber_AsSsize_t(key, PyExc_IndexError);
        if (index == -1 && PyErr_Occurred()) {
            return NULL;
        }
        return Utf8String_getitem(self, index);
    }
    else if (PySlice_Check(key)) {
        Py_ssize_t start, stop, step, slicelength;
        if (PySlice_GetIndicesEx(key, self->length, &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }
        if (step != 1) {
            PyErr_SetString(PyExc_NotImplementedError, "Step slicing not supported");
            return NULL;
        }
        return Utf8String_getslice(self, start, stop);
    }
    else {
        PyErr_Format(PyExc_TypeError, "string indices must be integers or slices, not %.200s",
                     Py_TYPE(key)->tp_name);
        return NULL;
    }
}

// __str__ method
static PyObject *Utf8String_str(Utf8StringObject *self) {
    return PyUnicode_FromStringAndSize(self->utf8_data, self->utf8_size);
}

// __repr__ method
static PyObject *Utf8String_repr(Utf8StringObject *self) {
    PyObject *str_obj = PyUnicode_FromStringAndSize(self->utf8_data, self->utf8_size);
    if (!str_obj) return NULL;

    PyObject *repr_obj = PyObject_Repr(str_obj);
    Py_DECREF(str_obj);
    return repr_obj;
}

// Hash function
static Py_hash_t Utf8String_hash(Utf8StringObject *self) {
    if (self->hash != -1) {
        return self->hash;
    }

    PyObject *str_obj = PyUnicode_FromStringAndSize(self->utf8_data, self->utf8_size);
    if (!str_obj) return -1;

    self->hash = PyObject_Hash(str_obj);
    Py_DECREF(str_obj);
    return self->hash;
}

// Compare with a str codepoint by codepoint. Asking the str for its UTF-8
// form instead would make CPython cache a UTF-8 copy inside every non-ASCII
// str this object is ever compared with.
static int Utf8String_equals_unicode(Utf8StringObject *self, PyObject *other) {
#if PY_VERSION_HEX < 0x030C0000
    if (PyUnicode_READY(other) < 0) {
        return -1;
    }
#endif
    if (PyUnicode_GET_LENGTH(other) != self->length) {
        return 0;
    }

    if (PyUnicode_IS_ASCII(other)) {
        // An ASCII str stores one byte per character, which is already UTF-8
        return self->utf8_size == self->length &&
               memcmp(self->utf8_data, PyUnicode_DATA(other), self->utf8_size) == 0;
    }

    int kind = PyUnicode_KIND(other);
    const void *data = PyUnicode_DATA(other);
    const unsigned char *p = (const unsigned char *)self->utf8_data;

    for (Py_ssize_t i = 0; i < self->length; i++) {
        int char_len = utf8_char_length(*p);
        if (utf8_decode(p, char_len) != PyUnicode_READ(kind, data, i)) {
            return 0;
        }
        p += char_len;
    }
    return 1;
}

// Equality comparison
static PyObject *Utf8String_richcompare(Utf8StringObject *self, PyObject *other, int op) {
    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int equal;

    if (Py_TYPE(other) == &Utf8StringType) {
        Utf8StringObject *other_utf8 = (Utf8StringObject *)other;
        equal = (self->utf8_size == other_utf8->utf8_size) &&
                (memcmp(self->utf8_data, other_utf8->utf8_data, self->utf8_size) == 0);
    }
    else if (PyUnicode_Check(other)) {
        equal = Utf8String_equals_unicode(self, other);
        if (equal < 0) return NULL;
    }
    else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (op == Py_EQ) {
        return PyBool_FromLong(equal);
    } else {
        return PyBool_FromLong(!equal);
    }
}

// __contains__ method: substring search, like str
static int Utf8String_contains(Utf8StringObject *self, PyObject *needle) {
    const char *data;
    Py_ssize_t size;
    PyObject *owner = NULL;

    if (Py_TYPE(needle) == &Utf8StringType) {
        data = ((Utf8StringObject *)needle)->utf8_data;
        size = ((Utf8StringObject *)needle)->utf8_size;
    }
    else if (PyUnicode_Check(needle)) {
        data = unicode_as_utf8(needle, &size, &owner);
        if (!data) {
            // A lone surrogate has no UTF-8 form, so it cannot occur in self
            if (PyErr_ExceptionMatches(PyExc_UnicodeEncodeError)) {
                PyErr_Clear();
                return 0;
            }
            return -1;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError, "'in <ministr>' requires string as left operand, not %.200s",
                     Py_TYPE(needle)->tp_name);
        return -1;
    }

    int found = bytes_contain(self->utf8_data, self->utf8_size, data, size);
    Py_XDECREF(owner);
    return found;
}

// __sizeof__ method: the object plus the buffers it owns
static PyObject *Utf8String_sizeof(Utf8StringObject *self, PyObject *Py_UNUSED(ignored)) {
    Py_ssize_t size = Py_TYPE(self)->tp_basicsize + self->utf8_size + 1;
    if (self->index) {
        size += index_entries(self) * sizeof(Py_ssize_t);
    }
    return PyLong_FromSsize_t(size);
}

static PyMethodDef Utf8String_methods[] = {
    {"__sizeof__", (PyCFunction)Utf8String_sizeof, METH_NOARGS, "Size in memory, in bytes"},
    {NULL, NULL, 0, NULL}
};

// Iterator: walks the UTF-8 bytes once, so iterating never needs the index
typedef struct {
    PyObject_HEAD
    Utf8StringObject *str;  // NULL once exhausted
    Py_ssize_t pos;         // byte offset of the next character
} Utf8StringIterObject;

static void Utf8StringIter_dealloc(Utf8StringIterObject *it) {
    Py_XDECREF(it->str);
    Py_TYPE(it)->tp_free((PyObject *)it);
}

static PyObject *Utf8StringIter_next(Utf8StringIterObject *it) {
    Utf8StringObject *str = it->str;
    if (!str) {
        return NULL;
    }

    if (it->pos >= str->utf8_size) {
        it->str = NULL;
        Py_DECREF(str);
        return NULL;
    }

    int char_len = utf8_char_length((unsigned char)str->utf8_data[it->pos]);
    PyObject *ch = PyUnicode_FromStringAndSize(str->utf8_data + it->pos, char_len);
    if (ch) {
        it->pos += char_len;
    }
    return ch;
}

static PyTypeObject Utf8StringIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "ministring.Utf8StringIterator",      // tp_name
    sizeof(Utf8StringIterObject),         // tp_basicsize
    0,                                    // tp_itemsize
    (destructor)Utf8StringIter_dealloc,   // tp_dealloc
    0,                                    // tp_vectorcall_offset
    0,                                    // tp_getattr
    0,                                    // tp_setattr
    0,                                    // tp_as_async
    0,                                    // tp_repr
    0,                                    // tp_as_number
    0,                                    // tp_as_sequence
    0,                                    // tp_as_mapping
    0,                                    // tp_hash
    0,                                    // tp_call
    0,                                    // tp_str
    0,                                    // tp_getattro
    0,                                    // tp_setattro
    0,                                    // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                   // tp_flags
    "Iterator over a Utf8String",         // tp_doc
    0,                                    // tp_traverse
    0,                                    // tp_clear
    0,                                    // tp_richcompare
    0,                                    // tp_weaklistoffset
    PyObject_SelfIter,                    // tp_iter
    (iternextfunc)Utf8StringIter_next,    // tp_iternext
};

// __iter__ method
static PyObject *Utf8String_iter(Utf8StringObject *self) {
    Utf8StringIterObject *it = PyObject_New(Utf8StringIterObject, &Utf8StringIterType);
    if (!it) {
        return NULL;
    }
    Py_INCREF(self);
    it->str = self;
    it->pos = 0;
    return (PyObject *)it;
}

// Sequence protocol
static PySequenceMethods Utf8String_as_sequence = {
    (lenfunc)Utf8String_length,     // sq_length
    0,                              // sq_concat
    0,                              // sq_repeat
    (ssizeargfunc)Utf8String_getitem, // sq_item
    0,                              // sq_slice (deprecated)
    0,                              // sq_ass_item
    0,                              // sq_ass_slice (deprecated)
    (objobjproc)Utf8String_contains, // sq_contains
    0,                              // sq_inplace_concat
    0,                              // sq_inplace_repeat
};

// Mapping protocol
static PyMappingMethods Utf8String_as_mapping = {
    (lenfunc)Utf8String_length,           // mp_length
    (binaryfunc)Utf8String_subscript,     // mp_subscript
    0,                                    // mp_ass_subscript
};

// Type definition
PyTypeObject Utf8StringType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "ministring.Utf8String",              // tp_name
    sizeof(Utf8StringObject),             // tp_basicsize
    0,                                    // tp_itemsize
    (destructor)Utf8String_dealloc,       // tp_dealloc
    0,                                    // tp_vectorcall_offset
    0,                                    // tp_getattr
    0,                                    // tp_setattr
    0,                                    // tp_as_async
    (reprfunc)Utf8String_repr,            // tp_repr
    0,                                    // tp_as_number
    &Utf8String_as_sequence,              // tp_as_sequence
    &Utf8String_as_mapping,               // tp_as_mapping
    (hashfunc)Utf8String_hash,            // tp_hash
    0,                                    // tp_call
    (reprfunc)Utf8String_str,             // tp_str
    0,                                    // tp_getattro
    0,                                    // tp_setattro
    0,                                    // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                   // tp_flags
    "Compact UTF-8 string type",          // tp_doc
    0,                                    // tp_traverse
    0,                                    // tp_clear
    (richcmpfunc)Utf8String_richcompare,  // tp_richcompare
    0,                                    // tp_weaklistoffset
    (getiterfunc)Utf8String_iter,         // tp_iter
    0,                                    // tp_iternext
    Utf8String_methods,                   // tp_methods
    0,                                    // tp_members
    0,                                    // tp_getset
    0,                                    // tp_base
    0,                                    // tp_dict
    0,                                    // tp_descr_get
    0,                                    // tp_descr_set
    0,                                    // tp_dictoffset
    0,                                    // tp_init
    0,                                    // tp_alloc
    0,                                    // tp_new
};

// Constructor function
PyObject *Utf8String_FromUTF8(const char *data, Py_ssize_t size) {
    if (size < 0) {
        size = strlen(data);
    }

    Utf8StringObject *self = (Utf8StringObject *)PyObject_New(Utf8StringObject, &Utf8StringType);
    if (!self) {
        return NULL;
    }

    // PyObject_New does not zero memory. Set every pointer the destructor
    // frees before the first allocation that can fail.
    self->utf8_data = NULL;
    self->index = NULL;
    self->hash = -1;

    // Copy UTF-8 data
    self->utf8_data = (char *)PyMem_Malloc(size + 1);
    if (!self->utf8_data) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }

    memcpy(self->utf8_data, data, size);
    self->utf8_data[size] = '\0';
    self->utf8_size = size;

    // Count codepoints
    self->length = utf8_codepoint_count(data, size);

    return (PyObject *)self;
}

// Create a Utf8String from a str, without leaving a UTF-8 copy cached
// inside it (see unicode_as_utf8)
static PyObject *Utf8String_FromUnicode(PyObject *obj) {
    PyObject *owner;
    Py_ssize_t size;
    const char *data = unicode_as_utf8(obj, &size, &owner);
    if (!data) return NULL;

    PyObject *result = Utf8String_FromUTF8(data, size);
    Py_XDECREF(owner);
    return result;
}

// Python wrapper for constructor
static PyObject *ministr_function(PyObject *self, PyObject *args) {
    PyObject *obj = NULL;

    if (!PyArg_ParseTuple(args, "|O", &obj)) {
        return NULL;
    }

    if (obj == NULL) {
        return Utf8String_FromUTF8("", 0);
    }

    if (PyUnicode_Check(obj)) {
        return Utf8String_FromUnicode(obj);
    }

    // Try to convert to string first
    PyObject *str_obj = PyObject_Str(obj);
    if (!str_obj) return NULL;

    PyObject *result = Utf8String_FromUnicode(str_obj);
    Py_DECREF(str_obj);
    return result;
}

// Module methods
static PyMethodDef ministring_methods[] = {
    {"ministr", ministr_function, METH_VARARGS, "Create a new Utf8String"},
    {NULL, NULL, 0, NULL}
};

// Module definition
static struct PyModuleDef ministring_module = {
    PyModuleDef_HEAD_INIT,
    "ministring",
    "Experimental compact UTF-8 string type for CPython",
    -1,
    ministring_methods
};

// Module initialization
PyMODINIT_FUNC PyInit_ministring(void) {
    PyObject *module;

    if (PyType_Ready(&Utf8StringType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&Utf8StringIterType) < 0) {
        return NULL;
    }

    module = PyModule_Create(&ministring_module);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&Utf8StringType);
    if (PyModule_AddObject(module, "Utf8String", (PyObject *)&Utf8StringType) < 0) {
        Py_DECREF(&Utf8StringType);
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
