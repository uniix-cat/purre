#ifndef PURR_EDITOR_H
#define PURR_EDITOR_H

#include "buffer.h"
#include "terminal.h"
#include <time.h>

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_VISUAL
} EditorMode;

typedef struct {
    char label[64];
    char insert_text[64];
    int kind;
} CompletionItem;

#define MAX_COMPLETIONS 32

typedef struct {
    int cx, cy;             /* Cursor buffer position (col, row) */
    int row_offset;         /* Scroll vertical offset */
    int col_offset;         /* Scroll horizontal offset */
    int screen_rows;        /* Terminal rows allocated for text */
    int screen_cols;        /* Terminal columns */
    EditorMode mode;
    Buffer buffer;
    
    char status_msg[128];
    time_t status_msg_time;
    
    char command_buf[256];
    int command_len;

    char lsp_status[64];
    
    int quit_requested;
    int pending_delete;     /* For 'd' operator waiting for second key */

    int visual_start_x;
    int visual_start_y;
    char *yank_buf;
    size_t yank_len;

    CompletionItem completions[MAX_COMPLETIONS];
    int num_completions;
    int completion_selected;
    int completion_active;

    void *lua_state;        /* lua_State pointer */
} Editor;

extern Editor E;

void editor_init(void);
void editor_cleanup(void);
void editor_set_status_message(const char *fmt, ...);
void editor_refresh_screen(void);
void editor_process_keypress(int c);
void editor_process_key_event(int key, const char *utf8, int len);
void editor_execute_command(const char *cmd);

void editor_clear_completions(void);
void editor_add_completion(const char *label, const char *insert_text, int kind);
void editor_accept_completion(void);

#endif /* PURR_EDITOR_H */
