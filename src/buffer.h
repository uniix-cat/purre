#ifndef PURR_BUFFER_H
#define PURR_BUFFER_H

#include <stddef.h>

typedef struct {
    char *chars;
    size_t len;
} erow;

typedef struct {
    int line;           /* 0-indexed line number */
    int col;
    int severity;       /* 1 = Error, 2 = Warning, 3 = Info, 4 = Hint */
    char *message;
} Diagnostic;

typedef struct {
    char *filename;
    char *filetype;
    erow *rows;
    int numrows;
    int dirty;
    Diagnostic *diagnostics;
    int num_diagnostics;
} Buffer;

void buffer_init(Buffer *b);
void buffer_free(Buffer *b);
void buffer_clear_diagnostics(Buffer *b);
void buffer_add_diagnostic(Buffer *b, int line, int col, int severity, const char *msg);
const Diagnostic *buffer_get_line_diagnostic(const Buffer *b, int line);
int buffer_open(Buffer *b, const char *filename);
int buffer_save(Buffer *b, const char *filename);
char *buffer_to_string(const Buffer *b, size_t *buflen);

void buffer_insert_row(Buffer *b, int at, const char *s, size_t len);
void buffer_delete_row(Buffer *b, int at);
void buffer_insert_char(Buffer *b, int row, int col, int c);
void buffer_insert_utf8(Buffer *b, int row, int *col, const char *utf8, int len);
void buffer_insert_newline(Buffer *b, int *row, int *col);
void buffer_delete_char(Buffer *b, int *row, int *col);

int buffer_prev_utf8_char(const erow *row, int col);
int buffer_next_utf8_char(const erow *row, int col);
int buffer_cx_to_rx(const erow *row, int cx);
int buffer_rx_to_cx(const erow *row, int rx);

const char *buffer_detect_filetype(const char *filename);

#endif /* PURR_BUFFER_H */
