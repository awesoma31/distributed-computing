#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "ipc.h"
#include "log.h"
#include "node.h"
#include "pa1.h"

static int fds[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1][2];

static int parse_args(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        return -1;
    }
    char *end;
    long x = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || x < 1 || x >= MAX_PROCESS_ID) {
        return -1;
    }
    return (int) x;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int open_pipes(int n) {
    FILE *out = fopen(pipes_log, "w");
    if (out == NULL) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                fds[i][j][0] = fds[i][j][1] = -1;
                continue;
            }
            if (pipe(fds[i][j]) != 0
                || set_nonblock(fds[i][j][0]) != 0
                || set_nonblock(fds[i][j][1]) != 0) {
                fclose(out);
                return -1;
            }
            fprintf(out, "pipe %d -> %d: read fd %d, write fd %d\n",
                    i, j, fds[i][j][0], fds[i][j][1]);
        }
    }
    fclose(out);
    return 0;
}

static void setup_node(node_t *node, local_id id, int n) {
    node->id = id;
    node->count = n;
    for (int i = 0; i <= MAX_PROCESS_ID; i++) {
        node->rd[i] = -1;
        node->wr[i] = -1;
    }
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                continue;
            }
            if (i == id) {
                node->wr[j] = fds[i][j][1];
                close(fds[i][j][0]);
            } else if (j == id) {
                node->rd[i] = fds[i][j][0];
                close(fds[i][j][1]);
            } else {
                close(fds[i][j][0]);
                close(fds[i][j][1]);
            }
        }
    }
}

static void close_node(node_t *node) {
    for (int i = 0; i < node->count; i++) {
        if (node->rd[i] >= 0) {
            close(node->rd[i]);
        }
        if (node->wr[i] >= 0) {
            close(node->wr[i]);
        }
    }
}

static int notify(node_t *node, MessageType type, const char *text) {
    Message msg;
    size_t len = strlen(text);
    msg.s_header.s_magic = MESSAGE_MAGIC;
    msg.s_header.s_type = type;
    msg.s_header.s_local_time = 0;
    msg.s_header.s_payload_len = len;
    memcpy(msg.s_payload, text, len);
    return send_multicast(node, &msg);
}

static int collect(node_t *node, MessageType type) {
    Message msg;
    for (local_id i = 1; i < node->count; i++) {
        if (i == node->id) {
            continue;
        }
        if (receive(node, i, &msg) != 0 || msg.s_header.s_type != type) {
            return -1;
        }
    }
    return 0;
}

static int run_child(node_t *node) {
    char buf[MAX_PAYLOAD_LEN];

    snprintf(buf, sizeof(buf), log_started_fmt, node->id, (int) getpid(), (int) getppid());
    log_line(buf);
    if (notify(node, STARTED, buf) != 0 || collect(node, STARTED) != 0) {
        return 1;
    }
    log_printf(log_received_all_started_fmt, node->id);

    snprintf(buf, sizeof(buf), log_done_fmt, node->id);
    log_line(buf);
    if (notify(node, DONE, buf) != 0 || collect(node, DONE) != 0) {
        return 1;
    }
    log_printf(log_received_all_done_fmt, node->id);
    return 0;
}

static int run_parent(node_t *node) {
    if (collect(node, STARTED) != 0) {
        return 1;
    }
    log_printf(log_received_all_started_fmt, node->id);
    if (collect(node, DONE) != 0) {
        return 1;
    }
    log_printf(log_received_all_done_fmt, node->id);
    return 0;
}

int main(int argc, char **argv) {
    int x = parse_args(argc, argv);
    if (x < 0) {
        fprintf(stderr, "usage: %s -p X\n", argv[0]);
        return 1;
    }
    int n = x + 1;

    if (log_open() != 0 || open_pipes(n) != 0) {
        perror("init");
        return 1;
    }

    for (local_id id = 1; id < n; id++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            node_t node;
            setup_node(&node, id, n);
            int code = run_child(&node);
            close_node(&node);
            log_close();
            exit(code);
        }
    }

    node_t node;
    setup_node(&node, PARENT_ID, n);
    int code = run_parent(&node);
    close_node(&node);

    int status;
    while (wait(&status) > 0) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            code = 1;
        }
    }
    log_close();
    return code;
}
