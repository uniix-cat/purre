#ifndef PURR_ASYNC_IO_H
#define PURR_ASYNC_IO_H

#include <stddef.h>
#include <sys/types.h>

typedef struct AsyncProcess AsyncProcess;

typedef void (*AsyncReadCallback)(AsyncProcess *proc, const char *data, size_t len, void *udata);
typedef void (*AsyncExitCallback)(AsyncProcess *proc, int exit_code, void *udata);

struct AsyncProcess {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int is_alive;
    AsyncReadCallback on_read;
    AsyncExitCallback on_exit;
    void *user_data;
    int lua_cb_ref;
    AsyncProcess *next;
};

void async_io_init(void);
void async_io_cleanup(void);

AsyncProcess *async_spawn(const char *cmd, char *const argv[],
                         AsyncReadCallback on_read,
                         AsyncExitCallback on_exit,
                         void *user_data);

int async_write(AsyncProcess *proc, const char *data, size_t len);
void async_close(AsyncProcess *proc);

/* Checks STDIN_FILENO and all active processes with timeout in ms.
 * Returns 1 if STDIN has input ready, 0 if timed out, -1 on error. */
int async_poll_events(int stdin_fd, int timeout_ms);

#endif /* PURR_ASYNC_IO_H */
