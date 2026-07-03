#include "log.h"
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int g_silent = 0;
static rtp_log_level_t g_log_level = LOG_LEVEL_INFO;
#ifdef _WIN32
static int g_console_initialized = 0;
static int g_use_ansi_colors = 0;

static void init_console_output(void) {
    HANDLE handle;
    DWORD mode;

    if (g_console_initialized) {
        return;
    }
    g_console_initialized = 1;

    handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        return;
    }

    if (!GetConsoleMode(handle, &mode)) {
        return;
    }

    if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        g_use_ansi_colors = 1;
    }
}
#endif

void rtp_log_set_level(rtp_log_level_t level) {
    g_log_level = level;
}

void rtp_log_set_silent(int silent) {
    g_silent = silent;
}

void rtp_log(rtp_log_level_t level, const char *fmt, ...) {
    if (g_silent || level < g_log_level) {
        return;
    }

    const char *level_strs[] = {"DEBUG", "INFO", "WARN", "ERROR", "SUCCESS"};
    const char *colors[] = {"\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[32m"};
    const char *reset = "\x1b[0m";

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

#ifdef _WIN32
    init_console_output();
    if (g_use_ansi_colors) {
        fprintf(stderr, "%s[%s] [%s]%s ", colors[level], time_buf, level_strs[level], reset);
    } else {
        fprintf(stderr, "[%s] [%s] ", time_buf, level_strs[level]);
    }
#else
    fprintf(stderr, "%s[%s] [%s]%s ", colors[level], time_buf, level_strs[level], reset);
#endif

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
