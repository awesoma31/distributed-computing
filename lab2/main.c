#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "banking.h"
#include "common.h"
#include "ipc.h"
#include "log.h"
#include "node.h"
#include "pa2345.h"

static int fds[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1][2];

typedef struct {
    balance_t balance;
    BalanceHistory history;
} account_t;

static int parse_args(int argc, char **argv, balance_t *balances) {
    if (argc < 3 || strcmp(argv[1], "-p") != 0) {
        return -1;
    }
    char *end;
    long x = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || x < 1 || x >= MAX_PROCESS_ID || argc != x + 3) {
        return -1;
    }
    for (int i = 0; i < x; i++) {
        long b = strtol(argv[i + 3], &end, 10);
        if (*argv[i + 3] == '\0' || *end != '\0' || b < 0) {
            return -1;
        }
        balances[i + 1] = (balance_t) b;
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

static void make_msg(Message *msg, MessageType type, const void *data, size_t len) {
    msg->s_header.s_magic = MESSAGE_MAGIC;
    msg->s_header.s_type = type;
    msg->s_header.s_local_time = get_physical_time();
    msg->s_header.s_payload_len = len;
    if (len > 0) {
        memcpy(msg->s_payload, data, len);
    }
}

static int notify(node_t *node, MessageType type, const char *text) {
    Message msg;
    make_msg(&msg, type, text, strlen(text));
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

static void track(account_t *acc, timestamp_t now) {
    BalanceHistory *h = &acc->history;
    balance_t last = h->s_history[h->s_history_len - 1].s_balance;
    for (int t = h->s_history_len; t <= now && t <= MAX_T; t++) {
        h->s_history[t].s_time = t;
        h->s_history[t].s_balance = last;
        h->s_history[t].s_balance_pending_in = 0;
        h->s_history_len = t + 1;
    }
    if (now <= MAX_T) {
        h->s_history[now].s_balance = acc->balance;
    }
}

static int handle_transfer(node_t *node, account_t *acc, Message *msg) {
    TransferOrder order;
    memcpy(&order, msg->s_payload, sizeof(order));
    timestamp_t now = get_physical_time();

    if (order.s_src == node->id) {
        acc->balance -= order.s_amount;
        track(acc, now);
        log_printf(log_transfer_out_fmt, now, node->id, order.s_amount, order.s_dst);
        msg->s_header.s_local_time = now;
        return send(node, order.s_dst, msg);
    }

    acc->balance += order.s_amount;
    track(acc, now);
    log_printf(log_transfer_in_fmt, now, node->id, order.s_amount, order.s_src);
    Message ack;
    make_msg(&ack, ACK, NULL, 0);
    return send(node, PARENT_ID, &ack);
}

static int run_child(node_t *node, balance_t start) {
    account_t acc;
    char buf[MAX_PAYLOAD_LEN];
    Message msg;
    int children = node->count - 1;
    int done = 0;
    int stopped = 0;

    acc.balance = start;
    acc.history.s_id = node->id;
    acc.history.s_history_len = 1;
    acc.history.s_history[0].s_time = 0;
    acc.history.s_history[0].s_balance = start;
    acc.history.s_history[0].s_balance_pending_in = 0;

    timestamp_t now = get_physical_time();
    track(&acc, now);
    snprintf(buf, sizeof(buf), log_started_fmt, now, node->id,
             (int) getpid(), (int) getppid(), acc.balance);
    log_line(buf);
    if (notify(node, STARTED, buf) != 0 || collect(node, STARTED) != 0) {
        return 1;
    }
    log_printf(log_received_all_started_fmt, get_physical_time(), node->id);

    while (!stopped || done < children - 1) {
        if (receive_any(node, &msg) != 0) {
            return 1;
        }
        switch (msg.s_header.s_type) {
        case TRANSFER:
            if (handle_transfer(node, &acc, &msg) != 0) {
                return 1;
            }
            break;
        case STOP:
            stopped = 1;
            now = get_physical_time();
            track(&acc, now);
            snprintf(buf, sizeof(buf), log_done_fmt, now, node->id, acc.balance);
            log_line(buf);
            if (notify(node, DONE, buf) != 0) {
                return 1;
            }
            break;
        case DONE:
            done++;
            break;
        default:
            break;
        }
    }

    now = get_physical_time();
    log_printf(log_received_all_done_fmt, now, node->id);
    track(&acc, now);

    size_t len = sizeof(acc.history) - sizeof(acc.history.s_history)
                 + acc.history.s_history_len * sizeof(BalanceState);
    make_msg(&msg, BALANCE_HISTORY, &acc.history, len);
    return send(node, PARENT_ID, &msg) != 0;
}

void transfer(void *parent_data, local_id src, local_id dst, balance_t amount) {
    node_t *node = parent_data;
    TransferOrder order = {src, dst, amount};
    Message msg;

    make_msg(&msg, TRANSFER, &order, sizeof(order));
    if (send(node, src, &msg) != 0) {
        return;
    }
    while (receive(node, dst, &msg) == 0) {
        if (msg.s_header.s_type == ACK) {
            return;
        }
    }
}

static int run_parent(node_t *node) {
    Message msg;
    AllHistory all;

    if (collect(node, STARTED) != 0) {
        return 1;
    }
    log_printf(log_received_all_started_fmt, get_physical_time(), node->id);

    bank_robbery(node, node->count - 1);

    make_msg(&msg, STOP, NULL, 0);
    if (send_multicast(node, &msg) != 0) {
        return 1;
    }
    if (collect(node, DONE) != 0) {
        return 1;
    }
    log_printf(log_received_all_done_fmt, get_physical_time(), node->id);

    all.s_history_len = node->count - 1;
    for (local_id i = 1; i < node->count; i++) {
        if (receive(node, i, &msg) != 0 || msg.s_header.s_type != BALANCE_HISTORY) {
            return 1;
        }
        memcpy(&all.s_history[i - 1], msg.s_payload, msg.s_header.s_payload_len);
    }
    print_history(&all);
    return 0;
}

int main(int argc, char **argv) {
    balance_t balances[MAX_PROCESS_ID + 1] = {0};
    int x = parse_args(argc, argv, balances);
    if (x < 0) {
        fprintf(stderr, "usage: %s -p X S1 ... SX\n", argv[0]);
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
            int code = run_child(&node, balances[id]);
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
