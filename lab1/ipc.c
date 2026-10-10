#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "ipc.h"
#include "node.h"

static int write_full(int fd, const char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, buf + done, len - done);
        if (n > 0) {
            done += n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int read_full(int fd, char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, buf + done, len - done);
        if (n > 0) {
            done += n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int read_body(int fd, Message *msg) {
    if (msg->s_header.s_magic != MESSAGE_MAGIC) {
        return -1;
    }
    if (msg->s_header.s_payload_len > MAX_PAYLOAD_LEN) {
        return -1;
    }
    return read_full(fd, msg->s_payload, msg->s_header.s_payload_len);
}

static int try_read(int fd, Message *msg) {
    char *buf = (char *) msg;
    size_t hdr = sizeof(MessageHeader);
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, hdr);
    int err = errno;
    fcntl(fd, F_SETFL, flags);

    if (n < 0) {
        return (err == EAGAIN || err == EINTR) ? 1 : -1;
    }
    if (n == 0) {
        return 1;
    }
    if ((size_t) n < hdr && read_full(fd, buf + n, hdr - n) != 0) {
        return -1;
    }
    return read_body(fd, msg);
}

int send(void *self, local_id dst, const Message *msg) {
    node_t *node = self;
    if (dst < 0 || dst >= node->count || dst == node->id || node->wr[dst] < 0) {
        return -1;
    }
    size_t len = sizeof(MessageHeader) + msg->s_header.s_payload_len;
    return write_full(node->wr[dst], (const char *) msg, len);
}

int send_multicast(void *self, const Message *msg) {
    node_t *node = self;
    for (local_id i = 0; i < node->count; i++) {
        if (i == node->id) {
            continue;
        }
        if (send(self, i, msg) != 0) {
            return -1;
        }
    }
    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    node_t *node = self;
    if (from < 0 || from >= node->count || from == node->id || node->rd[from] < 0) {
        return -1;
    }
    if (read_full(node->rd[from], (char *) msg, sizeof(MessageHeader)) != 0) {
        return -1;
    }
    return read_body(node->rd[from], msg);
}

int receive_any(void *self, Message *msg) {
    node_t *node = self;
    struct timespec pause = {0, 1000000};
    for (;;) {
        for (local_id i = 0; i < node->count; i++) {
            if (i == node->id || node->rd[i] < 0) {
                continue;
            }
            int res = try_read(node->rd[i], msg);
            if (res != 1) {
                return res;
            }
        }
        nanosleep(&pause, NULL);
    }
}
