#define _GNU_SOURCE
#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void buffer_init(Buffer *b) {
    b->filename = NULL;
    b->filetype = NULL;
    b->rows = NULL;
    b->numrows = 0;
    b->dirty = 0;
}

void buffer_free(Buffer *b) {
    if (!b) return;
    for (int i = 0; i < b->numrows; i++) {
        free(b->rows[i].chars);
    }
    free(b->rows);
    free(b->filename);
    free(b->filetype);
    buffer_init(b);
}

const char *buffer_detect_filetype(const char *filename) {
    if (!filename) return "text";
    const char *ext = strrchr(filename, '.');
    if (!ext) return "text";
    ext++;
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) return "c";
    if (strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 || strcmp(ext, "cc") == 0) return "cpp";
    if (strcmp(ext, "py") == 0) return "python";
    if (strcmp(ext, "lua") == 0) return "lua";
    if (strcmp(ext, "go") == 0) return "go";
    if (strcmp(ext, "rs") == 0) return "rust";
    if (strcmp(ext, "js") == 0 || strcmp(ext, "ts") == 0) return "javascript";
    if (strcmp(ext, "md") == 0) return "markdown";
    if (strcmp(ext, "json") == 0) return "json";
    if (strcmp(ext, "sh") == 0) return "bash";
    return "text";
}

void buffer_insert_row(Buffer *b, int at, const char *s, size_t len) {
    if (at < 0 || at > b->numrows) return;

    b->rows = realloc(b->rows, sizeof(erow) * (b->numrows + 1));
    if (at < b->numrows) {
        memmove(&b->rows[at + 1], &b->rows[at], sizeof(erow) * (b->numrows - at));
    }

    b->rows[at].len = len;
    b->rows[at].chars = malloc(len + 1);
    if (s && len > 0) {
        memcpy(b->rows[at].chars, s, len);
    }
    b->rows[at].chars[len] = '\0';
    b->numrows++;
    b->dirty++;
}

void buffer_delete_row(Buffer *b, int at) {
    if (at < 0 || at >= b->numrows) return;
    free(b->rows[at].chars);
    memmove(&b->rows[at], &b->rows[at + 1], sizeof(erow) * (b->numrows - at - 1));
    b->numrows--;
    b->dirty++;
}

static void buffer_row_append_string(erow *row, const char *s, size_t len) {
    row->chars = realloc(row->chars, row->len + len + 1);
    memcpy(&row->chars[row->len], s, len);
    row->len += len;
    row->chars[row->len] = '\0';
}

void buffer_insert_char(Buffer *b, int row, int col, int c) {
    if (b->numrows == 0) {
        buffer_insert_row(b, 0, "", 0);
    }
    if (row >= b->numrows) {
        row = b->numrows - 1;
    }
    erow *r = &b->rows[row];
    if (col < 0 || col > (int)r->len) {
        col = (int)r->len;
    }
    r->chars = realloc(r->chars, r->len + 2);
    memmove(&r->chars[col + 1], &r->chars[col], r->len - col + 1);
    r->len++;
    r->chars[col] = (char)c;
    b->dirty++;
}

void buffer_insert_newline(Buffer *b, int *row, int *col) {
    if (b->numrows == 0) {
        buffer_insert_row(b, 0, "", 0);
        buffer_insert_row(b, 1, "", 0);
        *row = 1;
        *col = 0;
        return;
    }

    int r = *row;
    int c = *col;
    if (r >= b->numrows) r = b->numrows - 1;

    erow *cur = &b->rows[r];
    if (c >= (int)cur->len) {
        buffer_insert_row(b, r + 1, "", 0);
    } else {
        buffer_insert_row(b, r + 1, &cur->chars[c], cur->len - c);
        cur = &b->rows[r]; /* refresh pointer after realloc */
        cur->len = c;
        cur->chars[c] = '\0';
    }
    *row = r + 1;
    *col = 0;
}

void buffer_delete_char(Buffer *b, int *row, int *col) {
    if (b->numrows == 0) return;
    int r = *row;
    int c = *col;
    if (r >= b->numrows) return;

    if (c == 0 && r > 0) {
        /* Merge with previous line */
        int prev_len = (int)b->rows[r - 1].len;
        buffer_row_append_string(&b->rows[r - 1], b->rows[r].chars, b->rows[r].len);
        buffer_delete_row(b, r);
        *row = r - 1;
        *col = prev_len;
        b->dirty++;
    } else if (c > 0) {
        erow *cur = &b->rows[r];
        if (c - 1 < (int)cur->len) {
            memmove(&cur->chars[c - 1], &cur->chars[c], cur->len - c + 1);
            cur->len--;
            *col = c - 1;
            b->dirty++;
        }
    }
}

int buffer_open(Buffer *b, const char *filename) {
    buffer_free(b);
    b->filename = strdup(filename);
    b->filetype = strdup(buffer_detect_filetype(filename));

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* New file, start with empty row */
        buffer_insert_row(b, 0, "", 0);
        b->dirty = 0;
        return 0;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        buffer_insert_row(b, b->numrows, line, linelen);
    }
    free(line);
    fclose(fp);

    if (b->numrows == 0) {
        buffer_insert_row(b, 0, "", 0);
    }
    b->dirty = 0;
    return 0;
}

char *buffer_to_string(const Buffer *b, size_t *buflen) {
    size_t totlen = 0;
    for (int i = 0; i < b->numrows; i++) {
        totlen += b->rows[i].len + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen + 1);
    char *p = buf;
    for (int i = 0; i < b->numrows; i++) {
        memcpy(p, b->rows[i].chars, b->rows[i].len);
        p += b->rows[i].len;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

int buffer_save(Buffer *b, const char *filename) {
    const char *path = filename ? filename : b->filename;
    if (!path) return -1;

    size_t len = 0;
    char *buf = buffer_to_string(b, &len);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        if (write(fd, buf, len) == (ssize_t)len) {
            close(fd);
            free(buf);
            b->dirty = 0;
            if (!b->filename || strcmp(b->filename, path) != 0) {
                free(b->filename);
                free(b->filetype);
                b->filename = strdup(path);
                b->filetype = strdup(buffer_detect_filetype(path));
            }
            return 0;
        }
        close(fd);
    }
    free(buf);
    return -1;
}
