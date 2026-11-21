#ifndef PURR_TERMINAL_H
#define PURR_TERMINAL_H

#include <stddef.h>
#include <termios.h>

enum EditorKey {
    KEY_NONE = 0,
    KEY_BACKSPACE = 127,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_ESC = 27,
    KEY_ENTER = '\r'
};

#define CTRL_KEY(k) ((k) & 0x1f)

/* Dynamic append buffer for flicker-free rendering */
typedef struct {
    char *b;
    size_t len;
    size_t cap;
} AppendBuffer;

void abuf_init(AppendBuffer *ab);
void abuf_append(AppendBuffer *ab, const char *s, size_t len);
void abuf_appends(AppendBuffer *ab, const char *s);
void abuf_printf(AppendBuffer *ab, const char *fmt, ...);
void abuf_free(AppendBuffer *ab);

int terminal_enable_raw_mode(void);
void terminal_disable_raw_mode(void);
int terminal_get_window_size(int *rows, int *cols);
int terminal_read_key(int fd);

#endif /* PURR_TERMINAL_H */
