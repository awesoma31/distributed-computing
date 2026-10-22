#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "ipc.h"
#include "log.h"

static int events_fd = -1;

int log_open(void) {
    events_fd = open(events_log, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    return events_fd < 0 ? -1 : 0;
}

void log_line(const char *text) {
    size_t len = strlen(text);
    if (write(STDOUT_FILENO, text, len) < 0) {
        return;
    }
    if (events_fd >= 0 && write(events_fd, text, len) < 0) {
        return;
    }
}

void log_printf(const char *fmt, ...) {
    char buf[MAX_PAYLOAD_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_line(buf);
}

void log_close(void) {
    if (events_fd >= 0) {
        close(events_fd);
        events_fd = -1;
    }
}
