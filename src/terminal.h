#ifndef PURR_TERMINAL_H
#define PURR_TERMINAL_H

#include <stddef.h>
#include <termios.h>

enum EditorKey {
    KEY_NONE = 0,
    KEY_CHAR = 1,
    KEY_BACKSPACE = 127,
    KEY_ESC = 27,
    KEY_ENTER = '\r',
    KEY_CTRL_C = 3,
    KEY_CTRL_S = 19,
    KEY_CTRL_Q = 17,
    
    /* Special keys placed well above Unicode plane 1 */
    KEY_ARROW_LEFT = 200000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN
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

/* Reads a key event. For normal characters (including multi-byte UTF-8 like Russian),
 * fills utf8_out (null-terminated) and returns KEY_CHAR.
 * For special keys, returns enum EditorKey. */
int terminal_read_key_utf8(int fd, char *utf8_out, int *utf8_len);

int utf8_visible_width(const char *s);

#endif /* PURR_TERMINAL_H */
