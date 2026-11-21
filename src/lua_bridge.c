#define _GNU_SOURCE
#include "lua_bridge.h"
#include "editor.h"
#include "async_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static lua_State *L = NULL;

typedef struct {
    AsyncProcess *proc;
    int lua_cb_ref;
    int id;
} LuaProcEntry;

#define MAX_LUA_PROCS 32
static LuaProcEntry lua_procs[MAX_LUA_PROCS];
static int next_proc_id = 1;

static void on_async_read(AsyncProcess *proc, const char *data, size_t len, void *udata) {
    (void)proc;
    LuaProcEntry *entry = (LuaProcEntry *)udata;
    if (!L || !entry || entry->lua_cb_ref == LUA_NOREF) return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, entry->lua_cb_ref);
    if (lua_isfunction(L, -1)) {
        lua_pushlstring(L, data, len);
        if (lua_pcall(L, 1, 0, 0) != 0) {
            editor_set_status_message("Lua async err: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

static void on_async_exit(AsyncProcess *proc, int exit_code, void *udata) {
    (void)proc;
    (void)exit_code;
    LuaProcEntry *entry = (LuaProcEntry *)udata;
    if (entry) {
        entry->proc = NULL;
    }
}

/* purr.set_status(msg) */
static int l_purr_set_status(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    editor_set_status_message("%s", msg);
    return 0;
}

/* purr.set_lsp_status(msg) */
static int l_purr_set_lsp_status(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    snprintf(E.lsp_status, sizeof(E.lsp_status), "%s", msg);
    return 0;
}

/* purr.get_filename() */
static int l_purr_get_filename(lua_State *L) {
    if (E.buffer.filename) {
        lua_pushstring(L, E.buffer.filename);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* purr.get_filetype() */
static int l_purr_get_filetype(lua_State *L) {
    if (E.buffer.filetype) {
        lua_pushstring(L, E.buffer.filetype);
    } else {
        lua_pushstring(L, "text");
    }
    return 1;
}

/* purr.get_buffer_text() */
static int l_purr_get_buffer_text(lua_State *L) {
    size_t len = 0;
    char *text = buffer_to_string(&E.buffer, &len);
    lua_pushlstring(L, text, len);
    free(text);
    return 1;
}

/* purr.get_cursor() -> row, col */
static int l_purr_get_cursor(lua_State *L) {
    lua_pushinteger(L, E.cy + 1);
    lua_pushinteger(L, E.cx + 1);
    return 2;
}

/* purr.set_cursor(row, col) */
static int l_purr_set_cursor(lua_State *L) {
    int row = (int)luaL_checkinteger(L, 1) - 1;
    int col = (int)luaL_checkinteger(L, 2) - 1;
    if (row < 0) row = 0;
    if (row >= E.buffer.numrows) row = E.buffer.numrows - 1;
    E.cy = row;
    if (col < 0) col = 0;
    if (E.cy < E.buffer.numrows && col > (int)E.buffer.rows[E.cy].len) {
        col = (int)E.buffer.rows[E.cy].len;
    }
    E.cx = col;
    return 0;
}

/* purr.insert_text(str) */
static int l_purr_insert_text(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n') {
            buffer_insert_newline(&E.buffer, &E.cy, &E.cx);
        } else {
            buffer_insert_char(&E.buffer, E.cy, E.cx, str[i]);
            E.cx++;
        }
    }
    return 0;
}

/* purr.save_file([path]) */
static int l_purr_save_file(lua_State *L) {
    const char *path = lua_isstring(L, 1) ? lua_tostring(L, 1) : NULL;
    int ret = buffer_save(&E.buffer, path);
    lua_pushboolean(L, ret == 0);
    return 1;
}

/* purr.open_file(path) */
static int l_purr_open_file(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int ret = buffer_open(&E.buffer, path);
    E.cx = 0;
    E.cy = 0;
    lua_pushboolean(L, ret == 0);
    return 1;
}

/* purr.spawn_async(cmd, args_table, callback_func) -> proc_id */
static int l_purr_spawn_async(lua_State *L) {
    const char *cmd = luaL_checkstring(L, 1);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    /* Build argv */
    int argc = 1;
    if (lua_istable(L, 2)) {
        argc += (int)lua_objlen(L, 2);
    }
    char **argv = malloc(sizeof(char*) * (argc + 1));
    argv[0] = strdup(cmd);
    if (lua_istable(L, 2)) {
        int n = (int)lua_objlen(L, 2);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, 2, i);
            argv[i] = strdup(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    argv[argc] = NULL;

    /* Store callback in registry */
    lua_pushvalue(L, 3);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Find empty proc slot */
    int slot = -1;
    for (int i = 0; i < MAX_LUA_PROCS; i++) {
        if (lua_procs[i].proc == NULL) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
        for (int i = 0; i < argc; i++) free(argv[i]);
        free(argv);
        lua_pushnil(L);
        return 1;
    }

    lua_procs[slot].lua_cb_ref = cb_ref;
    lua_procs[slot].id = next_proc_id++;

    AsyncProcess *proc = async_spawn(cmd, argv, on_async_read, on_async_exit, &lua_procs[slot]);
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);

    if (!proc) {
        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
        lua_procs[slot].lua_cb_ref = LUA_NOREF;
        lua_pushnil(L);
        return 1;
    }

    lua_procs[slot].proc = proc;
    lua_pushinteger(L, lua_procs[slot].id);
    return 1;
}

/* purr.send_stdin(proc_id, data) */
static int l_purr_send_stdin(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    size_t len = 0;
    const char *data = luaL_checklstring(L, 2, &len);

    for (int i = 0; i < MAX_LUA_PROCS; i++) {
        if (lua_procs[i].id == id && lua_procs[i].proc) {
            int ret = async_write(lua_procs[i].proc, data, len);
            lua_pushboolean(L, ret == 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

/* purr.close_proc(proc_id) */
static int l_purr_close_proc(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    for (int i = 0; i < MAX_LUA_PROCS; i++) {
        if (lua_procs[i].id == id && lua_procs[i].proc) {
            async_close(lua_procs[i].proc);
            lua_procs[i].proc = NULL;
            if (lua_procs[i].lua_cb_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, lua_procs[i].lua_cb_ref);
                lua_procs[i].lua_cb_ref = LUA_NOREF;
            }
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static const luaL_Reg purr_api[] = {
    {"set_status", l_purr_set_status},
    {"set_lsp_status", l_purr_set_lsp_status},
    {"get_filename", l_purr_get_filename},
    {"get_filetype", l_purr_get_filetype},
    {"get_buffer_text", l_purr_get_buffer_text},
    {"get_cursor", l_purr_get_cursor},
    {"set_cursor", l_purr_set_cursor},
    {"insert_text", l_purr_insert_text},
    {"save_file", l_purr_save_file},
    {"open_file", l_purr_open_file},
    {"spawn_async", l_purr_spawn_async},
    {"send_stdin", l_purr_send_stdin},
    {"close_proc", l_purr_close_proc},
    {NULL, NULL}
};

void lua_bridge_init(void) {
    for (int i = 0; i < MAX_LUA_PROCS; i++) {
        lua_procs[i].proc = NULL;
        lua_procs[i].lua_cb_ref = LUA_NOREF;
        lua_procs[i].id = 0;
    }

    L = luaL_newstate();
    if (!L) return;

    luaL_openlibs(L);

    /* Register purr table */
    lua_newtable(L);
    for (const luaL_Reg *reg = purr_api; reg->name; reg++) {
        lua_pushcfunction(L, reg->func);
        lua_setfield(L, -2, reg->name);
    }
    lua_setglobal(L, "purr");

    E.lua_state = L;

    /* Search for runtime/init.lua */
    const char *paths[] = {
        "runtime/init.lua",
        "/usr/share/purr/runtime/init.lua",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], R_OK) == 0) {
            if (luaL_dofile(L, paths[i]) != 0) {
                fprintf(stderr, "Error loading %s: %s\n", paths[i], lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            break;
        }
    }
}

void lua_bridge_cleanup(void) {
    if (L) {
        for (int i = 0; i < MAX_LUA_PROCS; i++) {
            if (lua_procs[i].proc) {
                async_close(lua_procs[i].proc);
                lua_procs[i].proc = NULL;
            }
        }
        lua_close(L);
        L = NULL;
        E.lua_state = NULL;
    }
}

void lua_bridge_eval(const char *code) {
    if (!L || !code) return;
    if (luaL_dostring(L, code) != 0) {
        editor_set_status_message("Lua error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        editor_set_status_message("Lua executed successfully");
    }
}

void lua_bridge_on_file_opened(const char *filename, const char *filetype) {
    if (!L) return;
    lua_getglobal(L, "purr");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "on_file_opened");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, filename ? filename : "");
        lua_pushstring(L, filetype ? filetype : "text");
        if (lua_pcall(L, 2, 0, 0) != 0) {
            editor_set_status_message("LSP error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void lua_bridge_on_file_saved(const char *filename) {
    if (!L) return;
    lua_getglobal(L, "purr");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "on_file_saved");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, filename ? filename : "");
        if (lua_pcall(L, 1, 0, 0) != 0) {
            editor_set_status_message("LSP error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

int lua_bridge_execute_command(const char *cmd_name, const char *args) {
    if (!L) return 0;
    lua_getglobal(L, "purr");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }

    lua_getfield(L, -1, "commands");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return 0; }

    lua_getfield(L, -1, cmd_name);
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, args ? args : "");
        if (lua_pcall(L, 1, 0, 0) != 0) {
            editor_set_status_message("Command error: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 2);
        return 1;
    }
    lua_pop(L, 3);
    return 0;
}
