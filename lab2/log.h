#ifndef LAB_LOG_H
#define LAB_LOG_H

int log_open(void);
void log_line(const char *text);
void log_printf(const char *fmt, ...);
void log_close(void);

#endif
