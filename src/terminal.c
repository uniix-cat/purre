#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>

static struct termios orig_termios;
static int raw_mode_enabled = 0;
static int unread_char = -1;

void abuf_init(AppendBuffer *ab) {
    ab->b = NULL;
    ab->len = 0;
    ab->cap = 0;
}

void abuf_append(AppendBuffer *ab, const char *s, size_t len) {
    if (len == 0 || s == NULL) return;
    if (ab->len + len + 1 > ab->cap) {
        size_t new_cap = ab->cap ? ab->cap * 2 : 256;
        while (new_cap < ab->len + len + 1) new_cap *= 2;
        char *new_b = realloc(ab->b, new_cap);
        if (!new_b) return;
        ab->b = new_b;
        ab->cap = new_cap;
    }
    memcpy(ab->b + ab->len, s, len);
    ab->len += len;
    ab->b[ab->len] = '\0';
}

void abuf_appends(AppendBuffer *ab, const char *s) {
    if (s) abuf_append(ab, s, strlen(s));
}

void abuf_printf(AppendBuffer *ab, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        abuf_append(ab, buf, (size_t)n);
    }
}

void abuf_free(AppendBuffer *ab) {
    free(ab->b);
    ab->b = NULL;
    ab->len = 0;
    ab->cap = 0;
}

void terminal_disable_raw_mode(void) {
    if (raw_mode_enabled) {
        write(STDOUT_FILENO, "\x1b[?1049l\x1b[?25h", 14);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
    }
}

int terminal_enable_raw_mode(void) {
    if (!isatty(STDIN_FILENO)) {
        raw_mode_enabled = 0;
        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        return -1;
    }

    atexit(terminal_disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }

    raw_mode_enabled = 1;
    write(STDOUT_FILENO, "\x1b[?1049h\x1b[2J\x1b[H", 14);
    return 0;
}

int terminal_get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B\x1b[6n", 16) != 16) return -1;
        
        char buf[32];
        unsigned int i = 0;
        while (i < sizeof(buf) - 1) {
            if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
            if (buf[i] == 'R') break;
            i++;
        }
        buf[i] = '\0';
        if (buf[0] != '\x1b' || buf[1] != '[') return -1;
        if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
        return 0;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

int terminal_read_key_utf8(int fd, char *utf8_out, int *utf8_len) {
    utf8_out[0] = '\0';
    *utf8_len = 0;

    char c;
    if (unread_char != -1) {
        c = (char)unread_char;
        unread_char = -1;
    } else {
        ssize_t nread = read(fd, &c, 1);
        if (nread == 0) {
            if (!isatty(fd)) return CTRL_KEY('q');
            return KEY_NONE;
        }
        if (nread < 0) {
            if (errno == EAGAIN || errno == EINTR) return KEY_NONE;
            return KEY_NONE;
        }
    }

    /* Escape sequence handling */
    if (c == '\x1b') {
        char seq[5];
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 30) <= 0) {
            return KEY_ESC;
        }

        if (read(fd, &seq[0], 1) != 1) return KEY_ESC;
        if (seq[0] != '[' && seq[0] != 'O') {
            unread_char = (unsigned char)seq[0];
            return KEY_ESC;
        }

        if (read(fd, &seq[1], 1) != 1) return KEY_ESC;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(fd, &seq[2], 1) != 1) return KEY_ESC;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '3': return KEY_DEL;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return KEY_ESC;
    }

    /* Backspace, Enter, and Tab */
    if (c == '\t') return KEY_TAB;
    if (c == '\n' || c == '\r') return KEY_ENTER;
    if (c == 8 || c == 127) return KEY_BACKSPACE;

    /* Control keys (Ctrl+A..Z) */
    if ((unsigned char)c < 32) {
        return (unsigned char)c;
    }

    /* Single-byte ASCII */
    unsigned char uc = (unsigned char)c;
    if (uc < 0x80) {
        utf8_out[0] = c;
        utf8_out[1] = '\0';
        *utf8_len = 1;
        return KEY_CHAR;
    }

    /* Multi-byte UTF-8 sequence (Cyrillic, CJK, Emojis, etc.) */
    int need_bytes = 0;
    if ((uc & 0xE0) == 0xC0) {
        need_bytes = 1; /* 2-byte sequence (Russian Cyrillic is here!) */
    } else if ((uc & 0xF0) == 0xE0) {
        need_bytes = 2; /* 3-byte sequence */
    } else if ((uc & 0xF8) == 0xF0) {
        need_bytes = 3; /* 4-byte sequence */
    }

    utf8_out[0] = c;
    int bytes_read = 1;
    for (int i = 0; i < need_bytes; i++) {
        char next_b;
        if (read(fd, &next_b, 1) == 1) {
            utf8_out[bytes_read++] = next_b;
        } else {
            break;
        }
    }
    utf8_out[bytes_read] = '\0';
    *utf8_len = bytes_read;
    return KEY_CHAR;
}

int utf8_visible_width(const char *s) {
    if (!s) return 0;
    int width = 0;
    size_t i = 0;
    size_t len = strlen(s);

    while (i < len) {
        /* Skip ANSI escape codes like \x1b[38;2;...m */
        if (s[i] == '\x1b' && i + 1 < len && s[i + 1] == '[') {
            i += 2;
            while (i < len && s[i] != 'm') i++;
            if (i < len && s[i] == 'm') i++;
            continue;
        }

        unsigned char c = (unsigned char)s[i];
        int char_len = 1;
        if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;

        if (i + char_len > len) char_len = len - i;

        width += 1;
        i += char_len;
    }
    return width;
}
