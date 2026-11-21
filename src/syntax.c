#include "syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

static const char *c_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case", "default",
    "sizeof", "goto", "const", "volatile", "register", "extern", NULL
};

static const char *c_types[] = {
    "int", "long", "short", "char", "unsigned", "signed", "void", "double",
    "float", "size_t", "ssize_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "bool", "auto", "FILE", NULL
};

static const char *lua_keywords[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while", NULL
};

static const char *py_keywords[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "False", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
    "or", "pass", "raise", "return", "True", "try", "while", "with", "yield", NULL
};

static int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:{}!&|", c) != NULL;
}

static const char *get_color_escape(int hl_type) {
    switch (hl_type) {
        case HL_KEYWORD:  return "\x1b[1;38;2;255;121;198m"; /* Dracula Pink Bold */
        case HL_TYPE:     return "\x1b[38;2;139;233;253m";   /* Dracula Cyan */
        case HL_STRING:   return "\x1b[38;2;241;250;140m";   /* Dracula Yellow */
        case HL_COMMENT:  return "\x1b[38;2;98;114;164m";    /* Dracula Comment Gray */
        case HL_NUMBER:   return "\x1b[38;2;189;147;249m";   /* Dracula Purple */
        case HL_FUNCTION: return "\x1b[38;2;80;250;123m";    /* Dracula Green */
        case HL_PREPROC:  return "\x1b[38;2;255;184;108m";   /* Dracula Orange */
        default:          return "\x1b[38;2;248;248;242m";   /* Dracula Foreground White */
    }
}

void syntax_highlight_line(const Buffer *b, int row_idx, AppendBuffer *ab, int col_offset, int max_cols) {
    if (row_idx < 0 || row_idx >= b->numrows) return;
    const erow *row = &b->rows[row_idx];
    if (row->len == 0) return;

    const char **keywords = c_keywords;
    const char **types = c_types;
    const char *ft = b->filetype ? b->filetype : "text";

    int is_lua = (strcmp(ft, "lua") == 0);
    int is_python = (strcmp(ft, "python") == 0);

    if (is_lua) {
        keywords = lua_keywords;
        types = NULL;
    } else if (is_python) {
        keywords = py_keywords;
        types = NULL;
    }

    uint8_t *hl = malloc(row->len);
    memset(hl, HL_NORMAL, row->len);

    int in_string = 0;
    size_t i = 0;

    while (i < row->len) {
        char c = row->chars[i];

        /* Comments */
        if (!in_string) {
            if ((c == '/' && i + 1 < row->len && row->chars[i + 1] == '/') ||
                (is_python && c == '#') ||
                (is_lua && c == '-' && i + 1 < row->len && row->chars[i + 1] == '-')) {
                memset(&hl[i], HL_COMMENT, row->len - i);
                break;
            }
        }

        /* Strings */
        if (in_string) {
            hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->len) {
                hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == in_string) {
                in_string = 0;
            }
            i++;
            continue;
        } else {
            if (c == '"' || c == '\'') {
                in_string = c;
                hl[i] = HL_STRING;
                i++;
                continue;
            }
        }

        /* Preprocessor (C/C++) */
        if (i == 0 && c == '#' && !is_python) {
            while (i < row->len && !isspace(row->chars[i])) {
                hl[i] = HL_PREPROC;
                i++;
            }
            continue;
        }

        /* Numbers */
        if (isdigit(c) && (i == 0 || is_separator(row->chars[i - 1]))) {
            while (i < row->len && (isdigit(row->chars[i]) || row->chars[i] == '.' || row->chars[i] == 'x' || row->chars[i] == 'X')) {
                hl[i] = HL_NUMBER;
                i++;
            }
            continue;
        }

        /* Keywords and Types */
        if (i == 0 || is_separator(row->chars[i - 1])) {
            int found = 0;
            if (keywords) {
                for (int j = 0; keywords[j]; j++) {
                    size_t klen = strlen(keywords[j]);
                    if (i + klen <= row->len &&
                        memcmp(&row->chars[i], keywords[j], klen) == 0 &&
                        (i + klen == row->len || is_separator(row->chars[i + klen]))) {
                        memset(&hl[i], HL_KEYWORD, klen);
                        i += klen;
                        found = 1;
                        break;
                    }
                }
            }
            if (!found && types) {
                for (int j = 0; types[j]; j++) {
                    size_t tlen = strlen(types[j]);
                    if (i + tlen <= row->len &&
                        memcmp(&row->chars[i], types[j], tlen) == 0 &&
                        (i + tlen == row->len || is_separator(row->chars[i + tlen]))) {
                        memset(&hl[i], HL_TYPE, tlen);
                        i += tlen;
                        found = 1;
                        break;
                    }
                }
            }
            if (found) continue;
        }

        /* Function calls (identifier followed by '(') */
        if (isalpha(c) || c == '_') {
            size_t start = i;
            while (i < row->len && (isalnum(row->chars[i]) || row->chars[i] == '_')) {
                i++;
            }
            size_t next = i;
            while (next < row->len && isspace(row->chars[next])) next++;
            if (next < row->len && row->chars[next] == '(') {
                memset(&hl[start], HL_FUNCTION, i - start);
            }
            continue;
        }

        i++;
    }

    /* Render characters to AppendBuffer with colors */
    int current_color = -1;
    int col_drawn = 0;

    for (size_t col = col_offset; col < row->len && col_drawn < max_cols; col++) {
        int color = hl[col];
        if (color != current_color) {
            current_color = color;
            abuf_appends(ab, get_color_escape(color));
        }
        abuf_append(ab, &row->chars[col], 1);
        col_drawn++;
    }

    abuf_appends(ab, "\x1b[0m"); /* Reset colors at line end */
    free(hl);
}
