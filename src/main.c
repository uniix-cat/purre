#include "editor.h"
#include "terminal.h"
#include "async_io.h"
#include "lua_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    async_io_init();
    editor_init();

    if (terminal_enable_raw_mode() == -1) {
        perror("Failed to enable raw mode");
        return 1;
    }

    lua_bridge_init();

    if (argc >= 2) {
        buffer_open(&E.buffer, argv[1]);
        editor_set_status_message("\"%s\" %dL, %dB", argv[1], E.buffer.numrows, 
                                 (int)(E.buffer.numrows > 0 ? E.buffer.rows[0].len : 0));
        lua_bridge_on_file_opened(argv[1], E.buffer.filetype);
    } else {
        buffer_insert_row(&E.buffer, 0, "", 0);
        E.buffer.dirty = 0;
        editor_set_status_message("purr -- modal editor | Ctrl+S: save | Ctrl+Q: quit | :help");
    }

    while (!E.quit_requested) {
        editor_refresh_screen();

        /* Poll terminal input and background LSP server pipes */
        int ready = async_poll_events(STDIN_FILENO, 50);
        if (ready > 0) {
            char utf8[16];
            int len = 0;
            int key = terminal_read_key_utf8(STDIN_FILENO, utf8, &len);
            if (key != KEY_NONE) {
                editor_process_key_event(key, utf8, len);
            }
        }
    }

    lua_bridge_cleanup();
    terminal_disable_raw_mode();
    async_io_cleanup();
    editor_cleanup();

    return 0;
}
