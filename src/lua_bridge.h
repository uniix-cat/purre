#ifndef PURR_LUA_BRIDGE_H
#define PURR_LUA_BRIDGE_H

void lua_bridge_init(void);
void lua_bridge_cleanup(void);
void lua_bridge_eval(const char *code);
void lua_bridge_on_file_opened(const char *filename, const char *filetype);
void lua_bridge_on_file_saved(const char *filename);
void lua_bridge_request_completion(void);
int lua_bridge_execute_command(const char *cmd_name, const char *args);

#endif /* PURR_LUA_BRIDGE_H */
