#define _GNU_SOURCE
#include "editor.h"
#include "terminal.h"
#include "syntax.h"
#include "lua_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>

Editor E;

void editor_init(void) {
    E.cx = 0;
    E.cy = 0;
    E.row_offset = 0;
    E.col_offset = 0;
    E.screen_rows = 24;
    E.screen_cols = 80;
    E.mode = MODE_NORMAL;
    E.status_msg[0] = '\0';
    E.status_msg_time = 0;
    E.command_buf[0] = '\0';
    E.command_len = 0;
    snprintf(E.lsp_status, sizeof(E.lsp_status), "LSP: idle");
    E.quit_requested = 0;
    E.pending_delete = 0;
    E.lua_state = NULL;

    buffer_init(&E.buffer);

    int rows, cols;
    if (terminal_get_window_size(&rows, &cols) == 0) {
        E.screen_rows = rows - 2; /* 1 for status bar, 1 for message/command bar */
        E.screen_cols = cols;
    }
}

void editor_cleanup(void) {
    buffer_free(&E.buffer);
}

void editor_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_msg_time = time(NULL);
}

static const char *get_file_icon(const char *ft) {
    if (!ft) return " ";
    if (strcmp(ft, "c") == 0) return " ";
    if (strcmp(ft, "cpp") == 0) return " ";
    if (strcmp(ft, "python") == 0) return " ";
    if (strcmp(ft, "rust") == 0) return " ";
    if (strcmp(ft, "go") == 0) return " ";
    if (strcmp(ft, "lua") == 0) return " ";
    if (strcmp(ft, "javascript") == 0 || strcmp(ft, "typescript") == 0) return " ";
    if (strcmp(ft, "markdown") == 0) return " ";
    if (strcmp(ft, "json") == 0) return " ";
    if (strcmp(ft, "bash") == 0) return " ";
    return " ";
}

static int editor_gutter_width(void) {
    return 8;
}

static void editor_scroll(void) {
    int text_cols = E.screen_cols - editor_gutter_width();
    if (text_cols < 1) text_cols = 1;

    if (E.cy < E.row_offset) {
        E.row_offset = E.cy;
    }
    if (E.cy >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cy - E.screen_rows + 1;
    }

    if (E.cx < E.col_offset) {
        E.col_offset = E.cx;
    }
    if (E.cx >= E.col_offset + text_cols) {
        E.col_offset = E.cx - text_cols + 1;
    }
}

static void editor_draw_rows(AppendBuffer *ab) {
    int gw = editor_gutter_width();
    int text_cols = E.screen_cols - gw;
    if (text_cols < 1) text_cols = 1;

    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + E.row_offset;
        if (filerow >= E.buffer.numrows) {
            /* Tildes for empty lines */
            abuf_printf(ab, "\x1b[38;2;98;114;164m       │ ~\x1b[0m");
        } else {
            /* Check for diagnostic icon in gutter */
            const Diagnostic *d = buffer_get_line_diagnostic(&E.buffer, filerow);
            if (d) {
                if (d->severity == 1) {
                    /* Red Error icon 󰅚 */
                    abuf_printf(ab, "\x1b[38;2;255;85;85m 󰅚 %3d \x1b[38;2;98;114;164m│\x1b[0m ", (filerow + 1) % 1000);
                } else if (d->severity == 2) {
                    /* Yellow Warning icon 󰀪 */
                    abuf_printf(ab, "\x1b[38;2;241;250;140m 󰀪 %3d \x1b[38;2;98;114;164m│\x1b[0m ", (filerow + 1) % 1000);
                } else {
                    /* Cyan Info icon 󰋽 */
                    abuf_printf(ab, "\x1b[38;2;139;233;253m 󰋽 %3d \x1b[38;2;98;114;164m│\x1b[0m ", (filerow + 1) % 1000);
                }
            } else {
                /* Dim line number */
                abuf_printf(ab, "\x1b[38;2;98;114;164m   %3d │\x1b[0m ", (filerow + 1) % 1000);
            }

            /* Syntax highlighting for text */
            syntax_highlight_line(&E.buffer, filerow, ab, E.col_offset, text_cols);
        }

        abuf_appends(ab, "\x1b[K"); /* Clear to end of line */
        abuf_appends(ab, "\r\n");
    }
}

static void editor_draw_status_bar(AppendBuffer *ab) {
    /* Dracula status bar */
    abuf_appends(ab, "\x1b[7m"); /* Invert colors */

    const char *mode_str = "󰄛 NORMAL";
    if (E.mode == MODE_INSERT) mode_str = "󰄛 INSERT";
    else if (E.mode == MODE_COMMAND) mode_str = "󰄛 COMMAND";

    const char *ficon = get_file_icon(E.buffer.filetype);

    /* Count diagnostics */
    int err_count = 0;
    int warn_count = 0;
    for (int i = 0; i < E.buffer.num_diagnostics; i++) {
        if (E.buffer.diagnostics[i].severity == 1) err_count++;
        else if (E.buffer.diagnostics[i].severity == 2) warn_count++;
    }

    char diag_badge[64] = "";
    if (err_count > 0 || warn_count > 0) {
        snprintf(diag_badge, sizeof(diag_badge), "󰅚 %d 󰀪 %d | ", err_count, warn_count);
    } else {
        snprintf(diag_badge, sizeof(diag_badge), "󰄬 clean | ");
    }

    char lstatus[128], rstatus[128];
    int len = snprintf(lstatus, sizeof(lstatus), " [ %s ] %s%s%s ",
                       mode_str,
                       ficon,
                       E.buffer.filename ? E.buffer.filename : "[No Name]",
                       E.buffer.dirty ? "  [+]" : "");

    int rlen = snprintf(rstatus, sizeof(rstatus), " %s%s | %s | Ln %d, Col %d ",
                        diag_badge,
                        E.lsp_status,
                        E.buffer.filetype ? E.buffer.filetype : "text",
                        E.cy + 1, E.cx + 1);

    if (len > E.screen_cols) len = E.screen_cols;
    abuf_append(ab, lstatus, len);

    while (len < E.screen_cols) {
        if (E.screen_cols - len == rlen) {
            abuf_append(ab, rstatus, rlen);
            break;
        } else {
            abuf_appends(ab, " ");
            len++;
        }
    }
    abuf_appends(ab, "\x1b[0m\r\n");
}

static void editor_draw_message_bar(AppendBuffer *ab) {
    abuf_appends(ab, "\x1b[K"); /* Clear line */

    if (E.mode == MODE_COMMAND) {
        abuf_printf(ab, ":%s", E.command_buf);
    } else {
        const Diagnostic *d = buffer_get_line_diagnostic(&E.buffer, E.cy);
        if (d) {
            const char *color = (d->severity == 1) ? "\x1b[1;38;2;255;85;85m" : "\x1b[1;38;2;241;250;140m";
            const char *icon = (d->severity == 1) ? "󰅚 " : "󰀪 ";
            abuf_printf(ab, "%s%s%s\x1b[0m", color, icon, d->message);
        } else {
            int msglen = strlen(E.status_msg);
            if (msglen > E.screen_cols) msglen = E.screen_cols;
            if (msglen && time(NULL) - E.status_msg_time < 5) {
                abuf_append(ab, E.status_msg, msglen);
            }
        }
    }
}

void editor_refresh_screen(void) {
    int rows, cols;
    if (terminal_get_window_size(&rows, &cols) == 0) {
        E.screen_rows = rows - 2;
        E.screen_cols = cols;
    }

    editor_scroll();

    AppendBuffer ab;
    abuf_init(&ab);

    abuf_appends(&ab, "\x1b[?25l"); /* Hide cursor */
    abuf_appends(&ab, "\x1b[H");    /* Cursor to home (1,1) */

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    /* Position cursor */
    if (E.mode == MODE_COMMAND) {
        abuf_printf(&ab, "\x1b[%d;%dH", E.screen_rows + 2, E.command_len + 2);
    } else {
        int cursor_y = (E.cy - E.row_offset) + 1;
        int cursor_x = (E.cx - E.col_offset) + 1 + editor_gutter_width();
        abuf_printf(&ab, "\x1b[%d;%dH", cursor_y, cursor_x);
    }

    abuf_appends(&ab, "\x1b[?25h"); /* Show cursor */

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}

void editor_execute_command(const char *cmd) {
    while (isspace(*cmd)) cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "q") == 0) {
        if (E.buffer.dirty) {
            editor_set_status_message("Warning: Unsaved changes! Use :q! or Ctrl+S to save");
            return;
        }
        E.quit_requested = 1;
    } else if (strcmp(cmd, "q!") == 0) {
        E.quit_requested = 1;
    } else if (strcmp(cmd, "w") == 0) {
        if (buffer_save(&E.buffer, NULL) == 0) {
            editor_set_status_message("File saved: %s", E.buffer.filename);
            lua_bridge_on_file_saved(E.buffer.filename);
        } else {
            editor_set_status_message("Error saving file!");
        }
    } else if (strncmp(cmd, "w ", 2) == 0) {
        const char *path = cmd + 2;
        while (isspace(*path)) path++;
        if (buffer_save(&E.buffer, path) == 0) {
            editor_set_status_message("File saved: %s", path);
            lua_bridge_on_file_saved(path);
        } else {
            editor_set_status_message("Error saving to %s", path);
        }
    } else if (strcmp(cmd, "wq") == 0) {
        if (buffer_save(&E.buffer, NULL) == 0) {
            lua_bridge_on_file_saved(E.buffer.filename);
            E.quit_requested = 1;
        } else {
            editor_set_status_message("Error saving file!");
        }
    } else if (strncmp(cmd, "e ", 2) == 0) {
        const char *path = cmd + 2;
        while (isspace(*path)) path++;
        buffer_open(&E.buffer, path);
        E.cx = 0;
        E.cy = 0;
        editor_set_status_message("Opened %s", path);
        lua_bridge_on_file_opened(path, E.buffer.filetype);
    } else if (strncmp(cmd, "lua ", 4) == 0) {
        lua_bridge_eval(cmd + 4);
    } else {
        /* Try executing as Lua registered command */
        char cmd_name[64];
        const char *args = "";
        const char *space = strchr(cmd, ' ');
        if (space) {
            size_t n = space - cmd;
            if (n >= sizeof(cmd_name)) n = sizeof(cmd_name) - 1;
            strncpy(cmd_name, cmd, n);
            cmd_name[n] = '\0';
            args = space + 1;
        } else {
            strncpy(cmd_name, cmd, sizeof(cmd_name) - 1);
            cmd_name[sizeof(cmd_name) - 1] = '\0';
        }

        if (!lua_bridge_execute_command(cmd_name, args)) {
            editor_set_status_message("Unknown command: :%s", cmd);
        }
    }
}

static void editor_move_cursor(int key) {
    erow *row = (E.cy >= 0 && E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
    int rowlen = row ? (int)row->len : 0;

    switch (key) {
        case 'j':
        case KEY_ARROW_LEFT:
            if (E.cx > 0) E.cx--;
            break;
        case ';':
        case KEY_ARROW_RIGHT:
            if (E.cx < rowlen) E.cx++;
            break;
        case 'l':
        case KEY_ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case 'k':
        case KEY_ARROW_DOWN:
            if (E.cy < E.buffer.numrows - 1) E.cy++;
            break;
    }

    row = (E.cy >= 0 && E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
    rowlen = row ? (int)row->len : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editor_process_keypress(int c) {
    if (c == KEY_NONE) return;

    /* Hybrid global shortcuts (work in ALL modes) */
    if (c == CTRL_KEY('s')) {
        if (buffer_save(&E.buffer, NULL) == 0) {
            editor_set_status_message("File saved: %s", E.buffer.filename);
            lua_bridge_on_file_saved(E.buffer.filename);
        } else {
            editor_set_status_message("Error saving file!");
        }
        return;
    }

    if (c == CTRL_KEY('q')) {
        if (E.buffer.dirty) {
            editor_set_status_message("Warning: Unsaved changes! Press :q! to force quit");
            return;
        }
        E.quit_requested = 1;
        return;
    }

    /* COMMAND MODE */
    if (E.mode == MODE_COMMAND) {
        if (c == KEY_ESC) {
            E.mode = MODE_NORMAL;
            E.command_len = 0;
            E.command_buf[0] = '\0';
        } else if (c == KEY_ENTER) {
            E.command_buf[E.command_len] = '\0';
            E.mode = MODE_NORMAL;
            editor_execute_command(E.command_buf);
            E.command_len = 0;
            E.command_buf[0] = '\0';
        } else if (c == KEY_BACKSPACE || c == KEY_DEL) {
            if (E.command_len > 0) {
                E.command_len--;
                E.command_buf[E.command_len] = '\0';
            } else {
                E.mode = MODE_NORMAL;
            }
        } else if (isprint(c)) {
            if (E.command_len < (int)sizeof(E.command_buf) - 1) {
                E.command_buf[E.command_len++] = (char)c;
                E.command_buf[E.command_len] = '\0';
            }
        }
        return;
    }

    /* INSERT MODE */
    if (E.mode == MODE_INSERT) {
        if (c == KEY_ESC) {
            E.mode = MODE_NORMAL;
        } else if (c == KEY_ENTER) {
            buffer_insert_newline(&E.buffer, &E.cy, &E.cx);
        } else if (c == KEY_BACKSPACE) {
            buffer_delete_char(&E.buffer, &E.cy, &E.cx);
        } else if (c == KEY_DEL) {
            if (E.cy < E.buffer.numrows) {
                int r = E.cy, col = E.cx + 1;
                if (col <= (int)E.buffer.rows[r].len) {
                    buffer_delete_char(&E.buffer, &r, &col);
                }
            }
        } else if (c == KEY_ARROW_LEFT || c == KEY_ARROW_RIGHT ||
                   c == KEY_ARROW_UP || c == KEY_ARROW_DOWN) {
            editor_move_cursor(c);
        } else if (isprint(c) || c == '\t') {
            if (c == '\t') {
                for (int i = 0; i < 4; i++) {
                    buffer_insert_char(&E.buffer, E.cy, E.cx++, ' ');
                }
            } else {
                buffer_insert_char(&E.buffer, E.cy, E.cx++, c);
            }
        }
        return;
    }

    /* NORMAL MODE */
    if (E.pending_delete) {
        if (c == 'd') {
            /* 'dd' deletes current line */
            if (E.buffer.numrows > 0) {
                buffer_delete_row(&E.buffer, E.cy);
                if (E.cy >= E.buffer.numrows && E.cy > 0) E.cy--;
                if (E.buffer.numrows == 0) buffer_insert_row(&E.buffer, 0, "", 0);
            }
        }
        E.pending_delete = 0;
        return;
    }

    switch (c) {
        case ':':
            E.mode = MODE_COMMAND;
            E.command_len = 0;
            E.command_buf[0] = '\0';
            break;
        case 'a':
            E.mode = MODE_INSERT;
            break;
        case 'o':
            buffer_insert_row(&E.buffer, E.cy + 1, "", 0);
            E.cy++;
            E.cx = 0;
            E.mode = MODE_INSERT;
            break;
        case 'O':
            buffer_insert_row(&E.buffer, E.cy, "", 0);
            E.cx = 0;
            E.mode = MODE_INSERT;
            break;
        case 'x':
            if (E.cy < E.buffer.numrows && E.cx < (int)E.buffer.rows[E.cy].len) {
                int r = E.cy, col = E.cx + 1;
                buffer_delete_char(&E.buffer, &r, &col);
            }
            break;
        case 'd':
            E.pending_delete = 1;
            break;
        case '0':
            E.cx = 0;
            break;
        case '$':
            if (E.cy < E.buffer.numrows) {
                E.cx = (int)E.buffer.rows[E.cy].len;
            }
            break;
        case 'g':
            E.cy = 0;
            E.cx = 0;
            break;
        case 'G':
            if (E.buffer.numrows > 0) {
                E.cy = E.buffer.numrows - 1;
                E.cx = 0;
            }
            break;
        case 'w': {
            /* Next word */
            if (E.cy < E.buffer.numrows) {
                erow *r = &E.buffer.rows[E.cy];
                while (E.cx < (int)r->len && !isspace(r->chars[E.cx])) E.cx++;
                while (E.cx < (int)r->len && isspace(r->chars[E.cx])) E.cx++;
                if (E.cx >= (int)r->len && E.cy + 1 < E.buffer.numrows) {
                    E.cy++;
                    E.cx = 0;
                }
            }
            break;
        }
        case 'b': {
            /* Prev word */
            if (E.cx > 0) {
                erow *r = &E.buffer.rows[E.cy];
                while (E.cx > 0 && isspace(r->chars[E.cx - 1])) E.cx--;
                while (E.cx > 0 && !isspace(r->chars[E.cx - 1])) E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                if (E.cy < E.buffer.numrows) {
                    E.cx = (int)E.buffer.rows[E.cy].len;
                }
            }
            break;
        }
        case 'j':
        case 'k':
        case 'l':
        case ';':
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
            editor_move_cursor(c);
            break;
        case 'K':
            /* Quick LSP hover documentation */
            lua_bridge_execute_command("lsp", "hover");
            break;
        case KEY_PAGE_UP:
            E.cy = E.row_offset - E.screen_rows;
            if (E.cy < 0) E.cy = 0;
            break;
        case KEY_PAGE_DOWN:
            E.cy = E.row_offset + E.screen_rows;
            if (E.cy >= E.buffer.numrows) E.cy = E.buffer.numrows - 1;
            break;
        case KEY_HOME:
            E.cx = 0;
            break;
        case KEY_END:
            if (E.cy < E.buffer.numrows) E.cx = (int)E.buffer.rows[E.cy].len;
            break;
    }
}
