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

static int editor_gutter_width(void) {
    return 8;
}

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
    editor_clear_completions();

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

void editor_clear_completions(void) {
    E.num_completions = 0;
    E.completion_selected = 0;
    E.completion_active = 0;
}

void editor_add_completion(const char *label, const char *insert_text, int kind) {
    if (E.num_completions >= MAX_COMPLETIONS) return;
    CompletionItem *item = &E.completions[E.num_completions++];
    strncpy(item->label, label ? label : "", sizeof(item->label) - 1);
    item->label[sizeof(item->label) - 1] = '\0';
    strncpy(item->insert_text, insert_text ? insert_text : item->label, sizeof(item->insert_text) - 1);
    item->insert_text[sizeof(item->insert_text) - 1] = '\0';
    item->kind = kind;
}

void editor_accept_completion(void) {
    if (!E.completion_active || E.num_completions == 0) return;
    CompletionItem *item = &E.completions[E.completion_selected];

    /* Find the prefix word before cursor to replace */
    erow *row = (E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
    if (row && E.cx > 0) {
        int start = E.cx - 1;
        while (start >= 0 && (isalnum((unsigned char)row->chars[start]) || row->chars[start] == '_')) {
            start--;
        }
        start++;
        int prefix_len = E.cx - start;
        if (prefix_len > 0) {
            memmove(&row->chars[start], &row->chars[E.cx], row->len - E.cx + 1);
            row->len -= prefix_len;
            E.cx = start;
        }
    }

    /* Insert completion text */
    int len = strlen(item->insert_text);
    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, item->insert_text, len);
    editor_clear_completions();
}

static const char *get_completion_kind_icon(int kind) {
    switch (kind) {
        case 2:
        case 3:  return "󰊕 "; /* Function / Method */
        case 4:  return "󰌋 "; /* Constructor */
        case 5:
        case 6:  return "󰆧 "; /* Variable / Field */
        case 7:
        case 8:  return "󰆧 "; /* Class / Interface */
        case 9:  return "󰏗 "; /* Module */
        case 14: return "󰌋 "; /* Keyword */
        case 15: return "󰉿 "; /* Snippet */
        default: return "󰛡 "; /* Text / Generic */
    }
}

static void editor_draw_completion_menu(AppendBuffer *ab) {
    if (!E.completion_active || E.num_completions == 0) return;

    erow *row = (E.cy >= 0 && E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
    int rx = buffer_cx_to_rx(row, E.cx);
    int cursor_y = (E.cy - E.row_offset) + 1;
    int cursor_x = (rx - E.col_offset) + 1 + editor_gutter_width();

    int menu_y = cursor_y + 1;
    int max_display = 6;
    int count = E.num_completions < max_display ? E.num_completions : max_display;
    if (menu_y + count > E.screen_rows + 1) {
        menu_y = cursor_y - count;
        if (menu_y < 1) menu_y = 1;
    }

    int menu_x = cursor_x;
    if (menu_x + 28 > E.screen_cols) {
        menu_x = E.screen_cols - 28;
        if (menu_x < 1) menu_x = 1;
    }

    abuf_appends(ab, "\x1b[s"); /* Save cursor position */

    for (int i = 0; i < count; i++) {
        CompletionItem *item = &E.completions[i];
        abuf_printf(ab, "\x1b[%d;%dH", menu_y + i, menu_x);

        if (i == E.completion_selected) {
            /* Dracula selection color */
            abuf_appends(ab, "\x1b[48;2;98;114;164;38;2;255;255;255m");
        } else {
            /* Dracula dark menu background */
            abuf_appends(ab, "\x1b[48;2;40;42;54;38;2;248;248;242m");
        }

        const char *kicon = get_completion_kind_icon(item->kind);
        char display_label[22];
        snprintf(display_label, sizeof(display_label), "%-20s", item->label);
        abuf_printf(ab, " %s%-20s ", kicon, display_label);
        abuf_appends(ab, "\x1b[0m");
    }

    abuf_appends(ab, "\x1b[u"); /* Restore cursor position */
}

static void editor_handle_newline(void) {
    if (E.cy >= E.buffer.numrows) {
        buffer_insert_newline(&E.buffer, &E.cy, &E.cx);
        return;
    }

    erow *cur = &E.buffer.rows[E.cy];
    int indent_len = 0;
    while (indent_len < (int)cur->len && (cur->chars[indent_len] == ' ' || cur->chars[indent_len] == '\t')) {
        indent_len++;
    }

    /* Check if Enter between { and } */
    int between_braces = 0;
    if (E.cx > 0 && E.cx < (int)cur->len) {
        char prev = cur->chars[E.cx - 1];
        char next = cur->chars[E.cx];
        if (prev == '{' && next == '}') {
            between_braces = 1;
        }
    }

    /* Extra indent if line ends with block starter */
    int extra_indent = 0;
    int k = E.cx - 1;
    while (k >= 0 && isspace((unsigned char)cur->chars[k])) k--;
    if (k >= 0) {
        char end_ch = cur->chars[k];
        if (end_ch == '{' || end_ch == ':' || end_ch == '(' || end_ch == '[') {
            extra_indent = 4;
        }
    }

    if (between_braces) {
        buffer_insert_newline(&E.buffer, &E.cy, &E.cx);
        if (indent_len > 0) {
            int zero = 0;
            buffer_insert_utf8(&E.buffer, E.cy, &zero, cur->chars, indent_len);
        }

        int target_cy = E.cy - 1;
        buffer_insert_row(&E.buffer, target_cy + 1, "", 0);
        E.cy = target_cy + 1;
        E.cx = 0;

        if (indent_len > 0) {
            buffer_insert_utf8(&E.buffer, E.cy, &E.cx, cur->chars, indent_len);
        }
        buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "    ", 4);
        return;
    }

    buffer_insert_newline(&E.buffer, &E.cy, &E.cx);
    if (indent_len > 0) {
        buffer_insert_utf8(&E.buffer, E.cy, &E.cx, cur->chars, indent_len);
    }
    if (extra_indent > 0) {
        buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "    ", 4);
    }
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

static void editor_scroll(void) {
    erow *row = (E.cy >= 0 && E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
    int rx = buffer_cx_to_rx(row, E.cx);
    int gw = editor_gutter_width();
    int text_cols = E.screen_cols - gw;
    if (text_cols < 1) text_cols = 1;

    if (E.cy < E.row_offset) {
        E.row_offset = E.cy;
    }
    if (E.cy >= E.row_offset + E.screen_rows) {
        E.row_offset = E.cy - E.screen_rows + 1;
    }

    if (rx < E.col_offset) {
        E.col_offset = rx;
    }
    if (rx >= E.col_offset + text_cols) {
        E.col_offset = rx - text_cols + 1;
    }
}

static void editor_draw_rows(AppendBuffer *ab) {
    int gw = editor_gutter_width();
    int text_cols = E.screen_cols - gw;
    if (text_cols < 1) text_cols = 1;

    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + E.row_offset;
        if (filerow >= E.buffer.numrows) {
            /* Tildes for empty lines: 6 spaces + '│' + 1 space = exactly 8 cols */
            abuf_printf(ab, "\x1b[38;2;98;114;164m      │ ~\x1b[0m");
        } else {
            /* Check for diagnostic icon in gutter: exactly 8 display columns */
            const Diagnostic *d = buffer_get_line_diagnostic(&E.buffer, filerow);
            if (d) {
                if (d->severity == 1) {
                    /* Red Error icon 󰅚: 1 cell icon + 4 cell num + 1 space + 1 cell '│' + 1 space = 8 cells */
                    abuf_printf(ab, "\x1b[38;2;255;85;85m󰅚%4d\x1b[38;2;98;114;164m │\x1b[0m ", (filerow + 1) % 10000);
                } else if (d->severity == 2) {
                    /* Yellow Warning icon 󰀪 */
                    abuf_printf(ab, "\x1b[38;2;241;250;140m󰀪%4d\x1b[38;2;98;114;164m │\x1b[0m ", (filerow + 1) % 10000);
                } else {
                    /* Cyan Info icon 󰋽 */
                    abuf_printf(ab, "\x1b[38;2;139;233;253m󰋽%4d\x1b[38;2;98;114;164m │\x1b[0m ", (filerow + 1) % 10000);
                }
            } else {
                /* Dim line number: 1 space + 4 digits + 1 space + '│' + 1 space = exactly 8 display cells */
                abuf_printf(ab, "\x1b[38;2;98;114;164m %4d │\x1b[0m ", (filerow + 1) % 10000);
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

    char lstatus[256], rstatus[256];
    snprintf(lstatus, sizeof(lstatus), " [ %s ] %s%s%s ",
             mode_str,
             ficon,
             E.buffer.filename ? E.buffer.filename : "[No Name]",
             E.buffer.dirty ? "  [+]" : "");

    snprintf(rstatus, sizeof(rstatus), " %s%s | %s | Ln %d, Col %d ",
             diag_badge,
             E.lsp_status,
             E.buffer.filetype ? E.buffer.filetype : "text",
             E.cy + 1, E.cx + 1);

    /* Calculate VISIBLE column widths (ignoring ANSI and accounting for UTF-8) */
    int lvis = utf8_visible_width(lstatus);
    int rvis = utf8_visible_width(rstatus);
    int pad = E.screen_cols - lvis - rvis;
    if (pad < 1) pad = 1;

    abuf_appends(ab, lstatus);
    for (int i = 0; i < pad; i++) {
        abuf_appends(ab, " ");
    }
    abuf_appends(ab, rstatus);
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
    editor_draw_completion_menu(&ab);

    /* Position cursor with visual render column (rx) */
    if (E.mode == MODE_COMMAND) {
        abuf_printf(&ab, "\x1b[%d;%dH", E.screen_rows + 2, E.command_len + 2);
    } else {
        erow *row = (E.cy >= 0 && E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
        int rx = buffer_cx_to_rx(row, E.cx);
        int cursor_y = (E.cy - E.row_offset) + 1;
        int cursor_x = (rx - E.col_offset) + 1 + editor_gutter_width();
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
            E.cx = buffer_prev_utf8_char(row, E.cx);
            break;
        case ';':
        case KEY_ARROW_RIGHT:
            E.cx = buffer_next_utf8_char(row, E.cx);
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

void editor_process_key_event(int key, const char *utf8, int len) {
    if (key == KEY_NONE) return;

    /* Hybrid global shortcuts (work in ALL modes) */
    if (key == CTRL_KEY('s') || key == KEY_CTRL_S) {
        if (buffer_save(&E.buffer, NULL) == 0) {
            editor_set_status_message("File saved: %s", E.buffer.filename);
            lua_bridge_on_file_saved(E.buffer.filename);
        } else {
            editor_set_status_message("Error saving file!");
        }
        return;
    }

    if (key == CTRL_KEY('q') || key == KEY_CTRL_Q) {
        if (E.buffer.dirty) {
            editor_set_status_message("Warning: Unsaved changes! Press :q! to force quit");
            return;
        }
        E.quit_requested = 1;
        return;
    }

    /* COMMAND MODE */
    if (E.mode == MODE_COMMAND) {
        if (key == KEY_ESC || key == CTRL_KEY('c') || key == KEY_CTRL_C) {
            E.mode = MODE_NORMAL;
            E.command_len = 0;
            E.command_buf[0] = '\0';
        } else if (key == KEY_ENTER) {
            E.command_buf[E.command_len] = '\0';
            E.mode = MODE_NORMAL;
            editor_execute_command(E.command_buf);
            E.command_len = 0;
            E.command_buf[0] = '\0';
        } else if (key == KEY_BACKSPACE || key == KEY_DEL) {
            if (E.command_len > 0) {
                int prev = buffer_prev_utf8_char(&(erow){.chars = E.command_buf, .len = E.command_len}, E.command_len);
                E.command_len = prev;
                E.command_buf[E.command_len] = '\0';
            } else {
                E.mode = MODE_NORMAL;
            }
        } else if (key == KEY_CHAR && len > 0) {
            if (E.command_len + len < (int)sizeof(E.command_buf) - 1) {
                memcpy(&E.command_buf[E.command_len], utf8, len);
                E.command_len += len;
                E.command_buf[E.command_len] = '\0';
            }
        }
        return;
    }

    /* INSERT MODE */
    if (E.mode == MODE_INSERT) {
        if (key == KEY_ESC || key == CTRL_KEY('c') || key == KEY_CTRL_C) {
            if (E.completion_active) {
                editor_clear_completions();
                return;
            }
            E.mode = MODE_NORMAL;
            return;
        }

        if (key == KEY_ENTER) {
            if (E.completion_active) {
                editor_accept_completion();
                return;
            }
            editor_handle_newline();
            return;
        }

        if (key == '\t') {
            if (E.completion_active && E.num_completions > 0) {
                /* Cycle through completion candidates */
                E.completion_selected = (E.completion_selected + 1) % E.num_completions;
                return;
            }

            /* Check if cursor is right after an identifier character */
            erow *row = (E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
            int has_word_prefix = 0;
            if (row && E.cx > 0) {
                char prev = row->chars[E.cx - 1];
                if (isalnum((unsigned char)prev) || prev == '_' || prev == '.' || prev == '>') {
                    has_word_prefix = 1;
                }
            }

            if (has_word_prefix) {
                lua_bridge_request_completion();
            } else {
                buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "    ", 4);
            }
            return;
        }

        if (key == KEY_BACKSPACE) {
            editor_clear_completions();
            erow *row = (E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
            if (row && E.cx > 0 && E.cx < (int)row->len) {
                char prev = row->chars[E.cx - 1];
                char curr = row->chars[E.cx];
                if ((prev == '(' && curr == ')') ||
                    (prev == '[' && curr == ']') ||
                    (prev == '{' && curr == '}') ||
                    (prev == '"' && curr == '"') ||
                    (prev == '\'' && curr == '\'')) {
                    buffer_delete_char(&E.buffer, &E.cy, &E.cx);
                    int r = E.cy, col = E.cx + 1;
                    buffer_delete_char(&E.buffer, &r, &col);
                    return;
                }
            }
            buffer_delete_char(&E.buffer, &E.cy, &E.cx);
            return;
        }

        if (key == KEY_DEL) {
            editor_clear_completions();
            if (E.cy < E.buffer.numrows) {
                erow *r = &E.buffer.rows[E.cy];
                int next_col = buffer_next_utf8_char(r, E.cx);
                if (next_col > E.cx) {
                    int bytes = next_col - E.cx;
                    memmove(&r->chars[E.cx], &r->chars[next_col], r->len - next_col + 1);
                    r->len -= bytes;
                    E.buffer.dirty++;
                }
            }
            return;
        }

        if (key == KEY_ARROW_LEFT || key == KEY_ARROW_RIGHT ||
            key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
            editor_clear_completions();
            editor_move_cursor(key);
            return;
        }

        if (key == KEY_CHAR && len > 0) {
            /* Close completion menu on space or non-identifier */
            if (len == 1) {
                char ch = utf8[0];
                erow *row = (E.cy < E.buffer.numrows) ? &E.buffer.rows[E.cy] : NULL;
                char next_ch = (row && E.cx < (int)row->len) ? row->chars[E.cx] : '\0';

                /* Overtype / step-over if typing closing bracket right before it */
                if ((ch == ')' && next_ch == ')') ||
                    (ch == ']' && next_ch == ']') ||
                    (ch == '}' && next_ch == '}') ||
                    (ch == '"' && next_ch == '"') ||
                    (ch == '\'' && next_ch == '\'')) {
                    E.cx++;
                    editor_clear_completions();
                    return;
                }

                /* Smart unindent when typing '}' on empty indented line */
                if (ch == '}') {
                    if (row && E.cx >= 4) {
                        int all_spaces = 1;
                        for (int i = 0; i < E.cx; i++) {
                            if (row->chars[i] != ' ' && row->chars[i] != '\t') { all_spaces = 0; break; }
                        }
                        if (all_spaces) {
                            for (int i = 0; i < 4; i++) {
                                buffer_delete_char(&E.buffer, &E.cy, &E.cx);
                            }
                        }
                    }
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "}", 1);
                    editor_clear_completions();
                    return;
                }

                /* Auto-closing pairs */
                if (ch == '(') {
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "()", 2);
                    E.cx--;
                    editor_clear_completions();
                    return;
                } else if (ch == '[') {
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "[]", 2);
                    E.cx--;
                    editor_clear_completions();
                    return;
                } else if (ch == '{') {
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "{}", 2);
                    E.cx--;
                    editor_clear_completions();
                    return;
                } else if (ch == '"') {
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "\"\"", 2);
                    E.cx--;
                    editor_clear_completions();
                    return;
                } else if (ch == '\'') {
                    buffer_insert_utf8(&E.buffer, E.cy, &E.cx, "''", 2);
                    E.cx--;
                    editor_clear_completions();
                    return;
                }
            }

            buffer_insert_utf8(&E.buffer, E.cy, &E.cx, utf8, len);
            editor_clear_completions();
        }
        return;
    }

    /* NORMAL MODE */
    if (key == CTRL_KEY('c') || key == KEY_CTRL_C) {
        E.pending_delete = 0;
        editor_set_status_message("Type :q to quit");
        return;
    }

    if (E.pending_delete) {
        if (key == KEY_CHAR && utf8[0] == 'd') {
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

    int c = (key == KEY_CHAR && len == 1) ? (unsigned char)utf8[0] : key;

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
            if (E.cy < E.buffer.numrows) {
                erow *r = &E.buffer.rows[E.cy];
                int next_col = buffer_next_utf8_char(r, E.cx);
                if (next_col > E.cx) {
                    int bytes = next_col - E.cx;
                    memmove(&r->chars[E.cx], &r->chars[next_col], r->len - next_col + 1);
                    r->len -= bytes;
                    E.buffer.dirty++;
                }
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

void editor_process_keypress(int c) {
    char utf8[8] = { (char)c, '\0' };
    editor_process_key_event(c, utf8, (c > 0 && c < 128) ? 1 : 0);
}
