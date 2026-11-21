#ifndef PURR_BUFFER_H
#define PURR_BUFFER_H

#include <stddef.h>

typedef struct {
    char *chars;
    size_t len;
} erow;

typedef struct {
    char *filename;
    char *filetype;
    erow *rows;
    int numrows;
    int dirty;
} Buffer;

void buffer_init(Buffer *b);
void buffer_free(Buffer *b);
int buffer_open(Buffer *b, const char *filename);
int buffer_save(Buffer *b, const char *filename);
char *buffer_to_string(const Buffer *b, size_t *buflen);

void buffer_insert_row(Buffer *b, int at, const char *s, size_t len);
void buffer_delete_row(Buffer *b, int at);
void buffer_insert_char(Buffer *b, int row, int col, int c);
void buffer_insert_newline(Buffer *b, int *row, int *col);
void buffer_delete_char(Buffer *b, int *row, int *col);

const char *buffer_detect_filetype(const char *filename);

#endif /* PURR_BUFFER_H */
