#define _GNU_SOURCE
#include "async_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>

static AsyncProcess *proc_list = NULL;

void async_io_init(void) {
    proc_list = NULL;
    /* Ignore SIGPIPE so writing to dead process doesn't kill the editor */
    signal(SIGPIPE, SIG_IGN);
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

AsyncProcess *async_spawn(const char *cmd, char *const argv[],
                         AsyncReadCallback on_read,
                         AsyncExitCallback on_exit,
                         void *user_data) {
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child process */
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        execvp(cmd, argv);
        _exit(127);
    }

    /* Parent process */
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    AsyncProcess *proc = calloc(1, sizeof(AsyncProcess));
    if (!proc) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);
        return NULL;
    }

    proc->pid = pid;
    proc->stdin_fd = in_pipe[1];
    proc->stdout_fd = out_pipe[0];
    proc->stderr_fd = err_pipe[0];
    proc->is_alive = 1;
    proc->on_read = on_read;
    proc->on_exit = on_exit;
    proc->user_data = user_data;
    proc->lua_cb_ref = -1;

    proc->next = proc_list;
    proc_list = proc;

    return proc;
}

int async_write(AsyncProcess *proc, const char *data, size_t len) {
    if (!proc || !proc->is_alive || proc->stdin_fd < 0) return -1;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(proc->stdin_fd, data + total, len - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        total += n;
    }
    return 0;
}

void async_close(AsyncProcess *proc) {
    if (!proc) return;
    if (proc->is_alive) {
        kill(proc->pid, SIGTERM);
        int status;
        waitpid(proc->pid, &status, WNOHANG);
        proc->is_alive = 0;
    }
    if (proc->stdin_fd >= 0) { close(proc->stdin_fd); proc->stdin_fd = -1; }
    if (proc->stdout_fd >= 0) { close(proc->stdout_fd); proc->stdout_fd = -1; }
    if (proc->stderr_fd >= 0) { close(proc->stderr_fd); proc->stderr_fd = -1; }
}

void async_io_cleanup(void) {
    AsyncProcess *cur = proc_list;
    while (cur) {
        AsyncProcess *next = cur->next;
        async_close(cur);
        free(cur);
        cur = next;
    }
    proc_list = NULL;
}

int async_poll_events(int stdin_fd, int timeout_ms) {
    /* Count active processes */
    int count = 1; /* stdin */
    for (AsyncProcess *p = proc_list; p; p = p->next) {
        if (p->is_alive && p->stdout_fd >= 0) {
            count++;
        }
    }

    struct pollfd fds[count];
    fds[0].fd = stdin_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    int idx = 1;
    AsyncProcess *map[count];
    for (AsyncProcess *p = proc_list; p; p = p->next) {
        if (p->is_alive && p->stdout_fd >= 0) {
            fds[idx].fd = p->stdout_fd;
            fds[idx].events = POLLIN;
            fds[idx].revents = 0;
            map[idx] = p;
            idx++;
        }
    }

    int ret = poll(fds, count, timeout_ms);
    if (ret <= 0) return ret;

    /* Read child process outputs */
    char buf[4096];
    for (int i = 1; i < idx; i++) {
        if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
            AsyncProcess *p = map[i];
            ssize_t n = read(p->stdout_fd, buf, sizeof(buf));
            if (n > 0) {
                if (p->on_read) {
                    p->on_read(p, buf, (size_t)n, p->user_data);
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                /* EOF or error: check if child terminated */
                int status = 0;
                pid_t wp = waitpid(p->pid, &status, WNOHANG);
                if (wp > 0) {
                    p->is_alive = 0;
                    if (p->on_exit) {
                        p->on_exit(p, WEXITSTATUS(status), p->user_data);
                    }
                }
            }
        }
    }

    return (fds[0].revents & POLLIN) ? 1 : 0;
}
