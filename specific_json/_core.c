#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>


#define PY311_PLUS (PY_VERSION_HEX >= 0x030b0000)
#define PY312_PLUS (PY_VERSION_HEX >= 0x030c0000)
#define PY313_PLUS (PY_VERSION_HEX >= 0x030d0000)
#define PY314_PLUS (PY_VERSION_HEX >= 0x030e0000)
#define PY315_PLUS (PY_VERSION_HEX >= 0x030f0000)

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')


typedef enum {
    JS_TOKEN_KEY,
    JS_TOKEN_INDEX,
} TokenType;

typedef struct {
    TokenType tp;
    union
    {
        struct {
            char *data;
            size_t length;
        } key;

        Py_ssize_t index;
    };
} PathToken;

typedef struct {
    PathToken *tokens;
    size_t size;
    size_t curr_index;
} JsonPath;

typedef struct {
    const char *start;
    const char *pos;
    const char *end;

    JsonPath *path; // may or may not be NULL
} DecoderState;



#define KEY_MAX_LENGTH 32


// Inspired by: https://github.com/msgspec/msgspec/blob/main/src/msgspec/_core.c#L17862 && https://github.com/msgspec/msgspec/blob/main/src/msgspec/_core.c#L159
#define REPEAT8(x) { x(0) x(1) x(2) x(3) x(4) x(5) x(6) x(7) }
#define CHECK_ASCII_BYTE(i)                                         \
    do {                                                            \
        const unsigned char c = (unsigned char)state->pos[(i)];     \
        if (c == '"') {                                             \
            state->pos += (i);                                      \
            goto no_escaped;                                        \
        }                                                           \
        else if (c == '\\') {                                       \
            state->pos += (i);                                      \
            goto escaped;                                           \
        }                                                           \
        else if (c < 0x20) {                                        \
            goto invalid_ctrl_char;                                 \
        }                                                           \
    } while (0);



static inline bool
get_jsonpath_from_args(JsonPath *path, PyObject *args, const Py_ssize_t nargs)
{
    assert(nargs > 1 && path != NULL && args != NULL);

    PathToken *tokens = PyMem_RawMalloc(sizeof(PathToken) * (nargs - 1));
    if (tokens == NULL) {
        PyErr_NoMemory();
        return false;
    }


    Py_ssize_t i;
    for (i = 1; i < nargs; i++) {
        PyObject *arg = PySequence_Fast_GET_ITEM(args, i);

        PathToken *token = &tokens[i-1];
        if (PyUnicode_Check(arg)) {
            token->tp = JS_TOKEN_KEY;
            token->key.data = NULL; // initalise

            Py_ssize_t length;
            const char *data = PyUnicode_AsUTF8AndSize(arg, &length);
            if (data == NULL) {
                goto error;
            }

            char *buffer = PyMem_RawMalloc(length);
            if (buffer == NULL) {
                goto error;
            }
            memcpy(buffer, data, length);
            token->key.data = buffer;
            token->key.length = length;
        }
        else if (PyLong_Check(arg)){
            token->tp = JS_TOKEN_INDEX;
            const Py_ssize_t index = PyLong_AsSsize_t(arg);
            if (PyErr_Occurred()) {
                goto error;
            }
            token->index = index;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Excpected an int (index) or str (key), got %.200s instead", Py_TYPE(arg)->tp_name);
            goto error;
        }
    }

    path->tokens = tokens;
    path->curr_index = 0;
    path->size = nargs -1;
    return true;

error:
    for (Py_ssize_t j = 0; j < i; j++) {
        PathToken *token = &tokens[j];
        if (token->tp == JS_TOKEN_KEY && token->key.data != NULL) {
            PyMem_RawFree(token->key.data);
        }
    }
    PyMem_RawFree(tokens);

    if (!PyErr_Occurred()) {
        // avoid supressing ValueError
        PyErr_NoMemory();
    }
    return false;
}

static inline void
destroy_json_path(JsonPath *path)
{
    if (path == NULL) {
        return;
    }

    for (size_t i = 0; i < path->size; i++) {
        PathToken *token = &path->tokens[i];
        if (token == NULL) {
            continue;
        }
        if (token->tp == JS_TOKEN_KEY) {
            PyMem_RawFree(token->key.data);
        }

    }

    PyMem_RawFree(path->tokens);
}


#define KEY_CACHE_SIZE 64
static PyObject *dict_key_cache[KEY_CACHE_SIZE]; // store keys that are less than 32 in size/length

static inline uint64_t
keycache_hash_bytes(const char *data, Py_ssize_t length)
{
    uint64_t hash = 14695981039346656037ULL;

    while (length--) {
        hash ^= (unsigned char)*data++;
        hash *= 1099511628211ULL;
    }

    return hash;
}


static inline PyObject *
Unc_object_from_ascii_nonascii_nonescape_string(const char *string, const Py_ssize_t length, const bool is_ascii)
{
    PyObject *key = NULL;
    if (is_ascii) {
        key = PyUnicode_New(length, 127);
        memcpy(PyUnicode_1BYTE_DATA(key), string, length);
    }
    else {
        key = PyUnicode_DecodeUTF8(string, length, "strict");
    }
    return key;
}


static size_t keycache_hits = 0;
static size_t keycache_misses = 0;
static size_t keycache_collisions = 0;

// returns the already inserted key if applicable
// else the newly created key
static inline PyObject *
keycache_insert(const char *string, const Py_ssize_t length, const bool is_ascii)
{
    assert(string != NULL && length >= 0);

    PyObject *key = NULL;
    if (length > KEY_MAX_LENGTH) {
        return Unc_object_from_ascii_nonascii_nonescape_string(string, length, is_ascii);
    }


    const uint64_t hash = keycache_hash_bytes(string, length);
    const uint32_t index = hash & (KEY_CACHE_SIZE - 1);

    PyObject *inital_entry = dict_key_cache[index];
    if (inital_entry != NULL) {
        Py_ssize_t cached_length;
        const char *cached = PyUnicode_AsUTF8AndSize(inital_entry, &cached_length);
        if (cached == NULL) {
            PyErr_SetString(PyExc_MemoryError, "Could not get string");
            return NULL;
        }
        if (length == cached_length && memcmp(cached, string, (size_t)length) == 0) {
            keycache_hits++;
            Py_INCREF(inital_entry);
            return inital_entry;
        }
        keycache_collisions++;
    }
    else {
        keycache_misses++;
    }


    key = Unc_object_from_ascii_nonascii_nonescape_string(string, length, is_ascii);
    if (key == NULL) {
        return NULL;
    }
    Py_CLEAR(inital_entry);
    dict_key_cache[index] = key;
    Py_INCREF(key);

    return key;
}

static inline void
keycache_clear()
{
    for (int i = 0; i < KEY_CACHE_SIZE; i++) {
        PyObject *key = dict_key_cache[i];
        if (key != NULL && Py_REFCNT(key) == 1) {
            dict_key_cache[i] = NULL;
            Py_DECREF(key);
        }
    }
}

static inline bool
is_valid_number_keyword_delimiter(const char *p, const char *end)
{
    if (p >= end) {
        return true;
    }

    switch (*p) {
        case ' ':
        case '\n':
        case '\t':
        case '\r':
        case ',':
        case ']':
        case '}':
            return true;
        default:
            return false;
    }
}


static inline PyObject *decode_json_value(DecoderState *state);
static inline bool skip_value(DecoderState *state);


static inline bool
skip_whitespaces(DecoderState *state, unsigned char *c)
{
    while (state->pos < state->end) {
        const unsigned char curr_char = *state->pos;
        if (curr_char == ' ' || curr_char == '\n' || curr_char == '\t' || curr_char == '\r') {
            state->pos++;
        }
        else {
            *c = curr_char;
            return true;
        }
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return false;
}

static inline bool
skip_string(DecoderState *state)
{
    assert(state);
    state->pos++;  // Skip opening quote.

    while (state->end - state->pos >= 8) {
        REPEAT8(CHECK_ASCII_BYTE);
        state->pos += 8;
    }

    // Handle remaining bytes.
    while (state->pos < state->end) {
        CHECK_ASCII_BYTE(0);
        state->pos++;
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return false;

no_escaped:
    state->pos++; // skip closing quote
    return true;

escaped:
    PyErr_Format(PyExc_ValueError, "Encountered an escape character: %.200s", state->pos);
    return false;

invalid_ctrl_char:
    PyErr_SetString(PyExc_ValueError, "Invalid control character in string");
    return false;
}


static inline bool
skip_int_or_float(DecoderState *state)
{
    if (*state->pos == '-') {
        state->pos++;
    }

    while (state->pos < state->end) {
        const unsigned char curr_char = *state->pos;
        if (!IS_DIGIT(curr_char) && curr_char != '.' && curr_char != 'e' && curr_char != 'E') {
            if (!is_valid_number_keyword_delimiter(state->pos, state->end)) {
                PyErr_Format(PyExc_ValueError, "Invalid number delimited '%c'", *state->pos);
                return false;
            }
            return true;
        }
        state->pos++;
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return false;
}


static inline bool
skip_bool_null(DecoderState *state, const char *to_skip, const int length)
{

    if (state->end - state->pos >= length && memcmp(state->pos, to_skip, length) == 0) {
        state->pos += length;
        if (!is_valid_number_keyword_delimiter(state->pos, state->end)) {
            PyErr_Format(PyExc_ValueError, "Invalid keyword delimited '%c'", *state->pos);
            return false;
        }
        return true;
    }

    PyErr_Format(PyExc_ValueError, "Expected '%s', got '%c...' instead ", to_skip, *state->pos);
    return false;
}


static inline bool
skip_list(DecoderState *state)
{
    state->pos++; // skip '['
    bool first = true;
    unsigned char c;
    while (state->pos < state->end) {
        if (!skip_whitespaces(state, &c)) {
            return false;
        }
        if (c == ']') {
            state->pos++;
            return true;
        }
        else if (c == ',' && !first) {
            state->pos++;
            if (!skip_whitespaces(state, &c)) {
                return false;
            }
        }
        else if (first) {
            first = false;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected ',' or ']', got '%c' instead", c);
            return false;
        }
    
        /* Skip value */
        if (!skip_value(state)) {
            return false;
        }
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return false;
}


static inline bool
skip_dict(DecoderState *state)
{
    state->pos++; // skip '{'
    bool first = true;
    unsigned char c;
    while (state->pos < state->end) {
        if (!skip_whitespaces(state, &c)) {
            return false;
        }
        if (c == '}') {
            state->pos++;
            return true;
        }
        else if (c == ',' && !first) {
            state->pos++;
            if (!skip_whitespaces(state, &c)) {
                return false;
            }
        }
        else if (first) {
            first = false;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected ',' or '}', got '%c' instead", c);
            return false;
        }

        /* Skip key */
        if (c == '"') {
            if (!skip_string(state)) {
                return false;
            }
        }
        else if (c == '}') {
            PyErr_SetString(PyExc_ValueError, "Trailing comma");
            return false;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected '\"', got '%c' instead", c);
            return false;
        }

        // Parse colon
        if (!skip_whitespaces(state, &c)) {
            return false;
        }
        if (c != ':') {
            PyErr_Format(PyExc_ValueError, "Expected ':', got '%c' instead", c);
            return false;
        }
        state->pos++;
        if (!skip_whitespaces(state, &c)) {
            return false;
        }

        if (!skip_value(state)) {
            return false;
        }
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return false;
}


static inline bool
skip_value(DecoderState *state)
{
    int length = 0;
    char *to_skip = NULL;
    switch (*state->pos)
    {
        case '"':
            return skip_string(state);
        case '{':
            return skip_dict(state);
        case '[':
            return skip_list(state);
        
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return skip_int_or_float(state);

        case 't':
            length = 4;
            to_skip = "true";
            break;

        case 'f':
            length = 5;
            to_skip = "false";
            break;

        case 'n':
            length = 4;
            to_skip = "null";
            break;

        default:
            PyErr_Format(PyExc_ValueError, "(Skip Value): Unexpected character '%c'", *state->pos);
            return false;
    }

    return skip_bool_null(state, to_skip, length);
}

// search through a dict to match the token string key
static inline PyObject *
dict_get_string(DecoderState *state, const PathToken *token, const bool decode, bool *err)
{
    state->pos++; // skip '{'
    unsigned char x;
    while (1) {
        if (!skip_whitespaces(state, &x)) {
            goto error;
        }

        if (x == '}') {
            goto key_error;
        }
        else if (x != '"') {
            state->pos++;
            continue;
        }

        state->pos++; // skip staring quote
        const char *start = state->pos;

        while (state->end - state->pos >= 8) {
            REPEAT8(CHECK_ASCII_BYTE);
            state->pos += 8;
        }

        /* Handle remaining bytes. */
        while (state->pos < state->end) {
            CHECK_ASCII_BYTE(0);
            state->pos++;
        }
        

        if (state->pos >= state->end) {
            goto end_of_buffer;
        }

no_escaped:
        const size_t length = state->pos - start;
        state->pos++; // skip closing quote

        if (!skip_whitespaces(state, &x)) {
            goto error;
        }

        if (x != ':') {
            PyErr_Format(PyExc_ValueError, "Expected ':', got '%c' instead", x);
            goto error;
        }
        state->pos++; // skip ':'

        if (!skip_whitespaces(state, &x)) {
            goto error;
        }

        if (length == token->key.length && memcmp(start, token->key.data, length) == 0) {
            state->path->curr_index++;
            if (decode) {
                PyObject *res = decode_json_value(state);
                // we don't have to set err here
                return res;
            }
            else {
                *err = true;
                return NULL;
            }
        }
        else if (!skip_value(state)) {
            goto error;
        }
    }

escaped:
    *err = false;
    PyErr_Format(PyExc_ValueError, "Encountered an escape character: %.200s", state->pos);
    return NULL;

key_error:
    *err = false;
    char token_buffer[51];
    const size_t length = Py_MIN(token->key.length, 50);
    memcpy(token_buffer, token->key.data, length);
    token_buffer[length] = '\0';
    PyErr_Format(PyExc_KeyError, "%s", token_buffer);
    return NULL;

end_of_buffer:
    *err = false;
    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return NULL;

invalid_ctrl_char:
    PyErr_SetString(PyExc_ValueError, "Invalid control character in string");
    return NULL;

error:
    *err = false;
    return NULL;
}

// search through a list to match the index
static inline PyObject *
list_get_index(DecoderState *state, const Py_ssize_t index_to_match, const bool decode, bool *err)
{
    bool first = true;
    size_t curr_index = 0;
    unsigned char c;
    state->pos++; // skip '['
    while (state->pos < state->end) {
        if (!skip_whitespaces(state, &c)) {
            *err = false;
            return NULL;
        }

        if (c == ']') {
            goto index_error;
        }
        else if (c == ',' && !first) {
            state->pos++;
            if (!skip_whitespaces(state, &c)) {
                *err = false;
                return NULL;
            }
        }
        else if (first) {
            first = false;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected ',' or ']', got '%c' instead", c);
            return NULL;
        }

        if (c == ']') {
            PyErr_SetString(PyExc_ValueError, "Trailing comma");
            *err = false;
            return NULL;
        }


        if (curr_index == index_to_match) {
            if (decode) {
                PyObject *res = decode_json_value(state);
                // err is not used when decoded
                return res;
            }
            else {
                *err = true;
                return NULL;
            }

        }
        else {
            if (!skip_value(state)) {
                *err = false;
                return NULL;
            }
        }

        curr_index++;
    }


    *err = false;
    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return NULL;

index_error:
    *err = false;
    PyErr_Format(PyExc_IndexError, "Index %zd out of range", index_to_match);
    return NULL;
}


// uses the cache
static inline PyObject *
parse_string_key(DecoderState *state)
{
    assert(state);

    state->pos++;  // Skip opening quote.
    const char *start = state->pos;
    bool is_ascii = true;

parse_start:
    while (state->end - state->pos >= 8) {
        REPEAT8(CHECK_ASCII_BYTE);
        state->pos += 8;
    }

    /* Handle remaining bytes. */
    while (state->pos < state->end) {
        CHECK_ASCII_BYTE(0);
        state->pos++;
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return NULL;

no_escaped:
    PyObject *ret = keycache_insert(start, state->pos - start, is_ascii);
    if (ret == NULL) {
        return NULL;
    }

    state->pos++;
    return ret;

escaped:
    PyErr_Format(PyExc_ValueError, "Encountered an escape character: %.200s", state->pos);
    return NULL;

invalid_ctrl_char:
    PyErr_SetString(PyExc_ValueError, "Invalid control character in string");
    return NULL;
}

static inline PyObject *
parse_string_value(DecoderState *state)
{
    assert(state);

    state->pos++;  // Skip opening quote.
    const char *start = state->pos;
    bool is_ascii = true;

    while (state->end - state->pos >= 8) {
        REPEAT8(CHECK_ASCII_BYTE);
        state->pos += 8;
    }

    // Handle remaining bytes
    while (state->pos < state->end) {
        CHECK_ASCII_BYTE(0);
        state->pos++;
    }

    PyErr_SetString(PyExc_ValueError, "Reached end of buffer");
    return NULL;

no_escaped:
    Py_ssize_t length = state->pos - start;
    PyObject *ret = Unc_object_from_ascii_nonascii_nonescape_string(start, length, is_ascii);
    if (ret == NULL) {
        return NULL;
    }
    state->pos++;
    return ret;

escaped:
    PyErr_Format(PyExc_ValueError, "Encountered an escape character: %.200s", state->pos);
    return NULL;

invalid_ctrl_char:
    PyErr_SetString(PyExc_ValueError, "Invalid control character in string");
    return NULL;
}


// -1 for error
// 0 for non (u)int64
// 1 for int64_t
// 2 for uint64_t
static inline int
parse_int64(DecoderState *state, int64_t *i64, uint64_t *u64, char **new_pos)
{
    bool negative = false;
    const char *p = state->pos;
    const char *end = state->end;
    uint64_t value = 0;
    unsigned char c = 0;


    assert(p < end);

    if (*p == '-') {
        negative = true;
        p++;

        if (p >= end) {
            PyErr_SetString(PyExc_ValueError, "Invalid number '-'");
            return -1;
        }
    }

    if (*p == '0') {
        p++;
        // Leading zeroes
        if (p < end && IS_DIGIT(*p)) {
            PyErr_SetString(PyExc_MemoryError, "Leading zeros are not allowed");
            return -1;
        }
    }
    else {
        // Inspired by: https://github.com/msgspec/msgspec/blob/main/src/msgspec/_core.c#L18716
        const size_t remaining = end - p;
        size_t n = Py_MIN(19, remaining);
        // 19 digits are definitely enough for u64
        // this allows us to skip overflow checks in the loop

        while (n) {
            c = *p;
            if (!IS_DIGIT(c)) {
                goto integer_not_0;
            }
            value = value * 10 + (uint64_t)(c - '0');
            n--;
            p++;
        }

        if (remaining > 19 && IS_DIGIT(*p))  {
            // fast path and overflow checking - TBD
            goto not_cinteger_no_error;
        }

integer_not_0:
        if (value == 0) {
            PyErr_SetString(PyExc_ValueError, "Invalid integer");
            return -1;
        }
    }


    c = *p;
    if (c == '.' || c == 'e' || c == 'E') {
        // floats and exponents
        goto not_cinteger_no_error;
    }

    if (!is_valid_number_keyword_delimiter(p, end)) {
        PyErr_SetString(PyExc_ValueError, "Invalid number delimiter");
        return -1;
    }


    int err = 1; // int64
    if (negative) {
        if (value > (1ULL << 63)) {
            goto not_cinteger_no_error;
        }

        if (value == (1ULL << 63)) {
            *i64 = INT64_MIN;
        }
        else {
            *i64 = -(int64_t)value;
        }
    }
    else {
        if (value > LLONG_MAX) {
            err = 2; // uint64
            *u64 = value;
        }
        else {
            *i64 = (int64_t)value;
        }
    }
    
    state->pos = p;
    return err;


not_cinteger_no_error:
    *new_pos = (char *)p;
    return 0;
}


#define STACK_BUFFER_LENGTH 128
static size_t int64_fast_path_hit = 0;
static size_t int64_fast_path_miss = 0;

static inline PyObject *
parse_int_or_float_value(DecoderState *state)
{
    assert(state);

    // fast int64/uint64 path
    char *new_pos;
    int64_t fast_signed_int_raw;
    uint64_t fast_usigned_int_raw;
    int err = parse_int64(state, &fast_signed_int_raw, &fast_usigned_int_raw, &new_pos);
    if (err == 1) {
        PyObject *ret = PyLong_FromLongLong(fast_signed_int_raw);

        if (ret == NULL) {
            return NULL;
        }
        int64_fast_path_hit++;

        return ret;
    }
    else if (err == 2) {
        PyObject *ret = PyLong_FromUnsignedLongLong(fast_usigned_int_raw);
        if (ret == NULL) {
            return NULL;
        }
        int64_fast_path_hit++;

        return ret;
    }
    else if (err == -1) {
        return NULL;
    }


    int64_fast_path_miss++;


    const char *inital_pos = state->pos;
    const char *p = new_pos;
    const char *end = state->end;
    bool is_float = false;

    // fallback to generic slow path
    if (*p >= '1' && *p <= '9') {
        while (p < end && IS_DIGIT(*p)) {
            p++;
        }
    }


    // Float
    if (*p == '.') {
        is_float = true;
        p++;

        if (p >= end || !IS_DIGIT(*p)) {
            goto invalid_number;
        }
        while (end - p >= 2) {
            if (!IS_DIGIT(p[0])) {
                break;
            }
            if (!IS_DIGIT(p[1])) {
                p += 1;
                break;
            }
            p += 2;
        }
        while (p < end) {
            if (!IS_DIGIT(*p)) {
                break;
            }
            p++;
        }
    }

    // Exponent
    if (p < end && (*p == 'e' || *p == 'E')) {
        is_float = true;
        p++;

        if (p < end && (*p == '+' || *p == '-')) {
            p++;
        }

        if (p >= end || !IS_DIGIT(*p)) {
            goto invalid_number;
        }
        p++;

        while (p < end && IS_DIGIT(*p)) {
            p++;
        }
    }

    const size_t length = p - inital_pos;

    PyObject *ret = NULL;
    char *conversion_end;
    if (is_float) {
        errno = 0;
        double value = strtod(inital_pos, &conversion_end);

        if (conversion_end != inital_pos + length || errno == ERANGE) {
            PyErr_SetString(PyExc_ValueError, "Invalid or out-of-range JSON number");
            return NULL;
        }

        ret = PyFloat_FromDouble(value);
    }
    else {
        char stack_buffer[STACK_BUFFER_LENGTH];
        char *buffer = stack_buffer;
        if (length > STACK_BUFFER_LENGTH) {
            buffer = PyMem_RawMalloc(length);
            if (buffer == NULL) {
                PyErr_SetString(PyExc_MemoryError, "Could not allocate buffer");
                return NULL;
            }
        }
        memcpy(buffer, inital_pos, length);
        buffer[length] = '\0';
        PyObject *temp = PyLong_FromString(buffer, &conversion_end, 10);

        if (temp == NULL) {
            if (length > STACK_BUFFER_LENGTH) {
                PyMem_RawFree(buffer);
            }
            goto invalid_number;
        }

        if (conversion_end != buffer + length) {
            Py_DECREF(temp);
            if (length > STACK_BUFFER_LENGTH) {
                PyMem_RawFree(buffer);
            }
            goto invalid_number;
        }


        if (length > STACK_BUFFER_LENGTH) {
            PyMem_RawFree(buffer);
        }

        ret = temp;
    }
    state->pos += length;
    return ret;

invalid_number:
    PyErr_SetString(PyExc_ValueError, "Invalid number reached");
    return NULL;
}

static inline PyObject *
parse_dict(DecoderState *state)
{
    state->pos++; // skip '{'
    PyObject *dict = PyDict_New();
    if (dict == NULL) {
        return NULL;
    }
    
    unsigned char c;

    PyObject *key = NULL;
    PyObject *value = NULL;
    bool first = true;

    // too lazy to split parsing the first key-value pair like what parse_list does
    while (1) {
        if (!skip_whitespaces(state, &c)) {
            goto error;
        }

        if (c == '}') {
            state->pos++;
            break;
        }
        else if (c == ',' && !first) {
            state->pos++;
            if (!skip_whitespaces(state, &c)) {
                goto error;
            }
        }
        else if (first) {
            first = false;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected a ',' or '}', got '%c' instead", c);
            goto error;
        }


        if (c == '"') {
            key = parse_string_key(state);
            if (key == NULL) {
                goto error;
            }
        }
        else if (c == '}') {
            PyErr_SetString(PyExc_ValueError, "Trailing commma");
            goto error;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected '\"' for a string key, got '%c' instead", c);
            goto error;
        }

        if (!skip_whitespaces(state, &c)) {
            goto error;
        }
        if (c != ':') {
            PyErr_Format(PyExc_ValueError, "Expected ':', got '%c' instead", c);
            goto error;
        }
        state->pos++; // skip ':'

        if (!skip_whitespaces(state, &c)) {
            goto error;
        }
        value = decode_json_value(state);
        if (value == NULL) {
            goto error;       
        }
        
        if (PyDict_SetItem(dict, key, value) < 0) {
            goto error;
        }

        Py_DECREF(key);
        key = NULL;

        Py_DECREF(value);
        value = NULL;
    }

    return dict;

error:
    Py_XDECREF(key);
    Py_XDECREF(value);
    Py_DECREF(dict);
    return NULL;
}


static size_t optimised_list_path = 0;
static size_t slow_list_path = 0;

// Inspired by: https://github.com/msgspec/msgspec/blob/main/src/msgspec/_core.c#L18293
#define LIST_CAPACITY(x) (((PyListObject *)x)->allocated)
#define ADD_VALUE_TO_LIST(array, c)                         \
 do {                                                       \
        PyObject *value = decode_json_value(state);         \
        if (value == NULL) {                                \
            goto error;                                     \
        }                                                   \
                                                            \
        if (LIST_CAPACITY((array)) > Py_SIZE((array))) {    \
            PyList_SET_ITEM(array, Py_SIZE((array)), value);\
            Py_SET_SIZE((array), Py_SIZE((array)) + 1);     \
            optimised_list_path++;                          \
        }                                                   \
        else {                                              \
            int status = PyList_Append((array), value);     \
            Py_DECREF(value);                               \
            slow_list_path++;                               \
            if (status < 0) goto error;                     \
        }                                                   \
                                                            \
        if (!skip_whitespaces(state, &(c))) {               \
            return NULL;                                    \
        }                                                   \
    } while (0)


static inline PyObject *
parse_list(DecoderState *state)
{
    assert(state);
    state->pos++; // skip '['
    PyObject *list = PyList_New(0);
    if (list == NULL) {
        return NULL;
    }

    unsigned char c;

    if (!skip_whitespaces(state, &c)) {
        return NULL;
    }

    if (c == ']') {
        state->pos++;
        return list;
    }
    else if (c == ',') {
        PyErr_SetString(PyExc_ValueError, "Trailing comma");
        goto error;
    }
    else {
        ADD_VALUE_TO_LIST(list, c); // already callls 'skip_whitespaces'
    }


    while (1) {
        if (c == ',') {
            state->pos++;
            if (!skip_whitespaces(state, &c)) {
                goto error;
            }
        }
        else if (c == ']') {
            state->pos++;
            break;
        }
        else {
            PyErr_Format(PyExc_ValueError, "Expected a ',' or ']', got '%.100s' instead", state->pos);
            goto error;
        }

        if (c == '}') {
            PyErr_SetString(PyExc_ValueError, "Trailing comma");
            goto error;
        }

        ADD_VALUE_TO_LIST(list, c);
    }
    return list;

error:
    Py_DECREF(list);
    return NULL;
}


static inline PyObject *
json_search_path(DecoderState *state)
{
    const size_t size = state->path->size;
    bool err = false;
    for (size_t i = 0; i < state->path->size; i++) {
        const PathToken *token = &state->path->tokens[i];
        if (token->tp == JS_TOKEN_KEY) {
            if (i == size - 1) {
                // decode object
                PyObject *obj = dict_get_string(state, token, true, &err);
                return obj;
            }

            dict_get_string(state, token, false, &err);
            if (err == false) {
                return NULL;
            }
        }
        else {
            if (i == size - 1) {
                // decode object
                PyObject *obj = list_get_index(state, token->index, true, &err);
                return obj;
            }

            list_get_index(state, token->index, false, &err);
            if (err == false) {
                return NULL;
            }
        }
    }

    return NULL;
}

static inline PyObject *
decode_json_value(DecoderState *state)
{
    assert(state && state->pos < state->end);

    const char *end = state->end;
    const char *p = state->pos;

    int length = 0;
    PyObject *ret = NULL;

    switch (*p)
    {
        case '"':
            return parse_string_value(state);
        case '{':
            return parse_dict(state);
        case '[':
            return parse_list(state);
        
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parse_int_or_float_value(state);


        /*
        Yes, I have tried doing (p[1] == 'r' || p[2] == 'u' ...)
        But it showed no performance gains

        And I have also tried incrementing p
        before comparing  
        Essentially: p++; memcmp(p, "rue", 3)
        */  
        case 't':
            length = 4;
            if (end - p < length || memcmp(p, "true", length))
                return NULL;
            ret = Py_True;
            break;

        case 'f':
            length = 5;
            if (end - p < length || memcmp(p, "false", length))
                return NULL;
            ret = Py_False;
            break;

        case 'n':
            length = 4;
            if (end - p < length || memcmp(p, "null", length))
                return NULL;
            ret = Py_None;
            break;

        default:
            PyErr_Format(
                PyExc_ValueError,
                "Unexpected character '%c'",
                *p
            );
            return NULL;
    }

    // true - false - null
    state->pos += length;

#if PY312_PLUS
    return ret;
#else
    // Singletons are not immortal in < 3.12
    return Py_NewRef(ret);
#endif
}


static PyObject *
json_decode(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const Py_ssize_t nargs = PyTuple_Size(args);
    if (nargs < 1) {
        PyErr_Format(PyExc_TypeError, "expected at least 1 argument, got %zd instead", nargs);
        return NULL;
    }
    PyObject *to_decode = PyTuple_GET_ITEM(args, 0);

    const char *json_str = NULL;
    Py_ssize_t length = 0;
    if (PyUnicode_Check(to_decode)) {
        json_str = PyUnicode_AsUTF8AndSize(to_decode, &length);
        if (json_str == NULL) {
            return NULL;
        }
    }
    else if (PyBytes_Check(to_decode)) {
        char *bytes;
        if (PyBytes_AsStringAndSize(to_decode, &bytes, &length) < 0) {
            return NULL;
        }
        json_str = bytes;
    }
    else {
        PyErr_Format(PyExc_TypeError, "Expected a str or bytes, got %.200s instead.", Py_TYPE(to_decode)->tp_name);
        return NULL;    
    }

    const char *end = json_str + length;
    DecoderState state = {.start = json_str, .pos = json_str, .end = end, .path=NULL};
    JsonPath path;

    if (nargs > 1) {
        if (!get_jsonpath_from_args(&path, args, nargs)) {
            return NULL;
        }
        state.path = &path;
    }

    unsigned char c;
    if (!skip_whitespaces(&state, &c)) {
        PyErr_SetString(PyExc_ValueError, "String does not contain any valid json objects");
        return NULL;
    }


    PyObject *ret;
    if (state.path != NULL) {
        ret = json_search_path(&state);
        destroy_json_path(state.path);

        if (kwargs != NULL) {
            PyObject *error = PyErr_Occurred();
            if (error == NULL) {
                return NULL;
            }
    
            PyObject *default_value = PyDict_GetItemString(kwargs, "default");
            if (default_value == NULL) {
                return NULL;
            }

            if (PyErr_GivenExceptionMatches(error, PyExc_KeyError) || PyErr_GivenExceptionMatches(error, PyExc_IndexError)) {
                PyErr_Clear();
                return Py_NewRef(default_value);
            }
        }
    }
    else {
        ret = decode_json_value(&state);
    }

    return ret;
}


static PyObject *
json_debug_stats(PyObject *self, PyObject *args)
{
    (void)args;
    printf("Key Cache - Hit: %zu, miss: %zu, collisions: %zu\n", keycache_hits, keycache_misses, keycache_collisions);
    printf("Int64 fast path - Hit: %zu, miss: %zu\n", int64_fast_path_hit, int64_fast_path_miss);
    printf("List Append fast path - Hit: %zu, miss: %zu\n", optimised_list_path, slow_list_path);
    
    printf("Key Cache: [");
    for (int i = 0; i < KEY_CACHE_SIZE; i++) {
        PyObject *key = dict_key_cache[i];
        if (key != NULL) {
            PyObject_Print(key, stdout, 0);
            printf(", ");
        }
    }
    printf("]");
    Py_RETURN_NONE;
}



static PyMethodDef methods[] = {
    {"json_decode", (PyCFunction)json_decode, METH_VARARGS | METH_KEYWORDS, "Decode a json str object"},
    {"json_debug_stats", (PyCFunction)json_debug_stats, METH_VARARGS, "Debug stats"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "specific_json",
    NULL,
    -1,
    methods
};

PyMODINIT_FUNC PyInit__core(void)
{
    return PyModule_Create(&module);
}