#ifndef RTP_LOG_H
#define RTP_LOG_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_SUCCESS
} rtp_log_level_t;

void rtp_log_set_level(rtp_log_level_t level);
void rtp_log_set_silent(int silent);

void rtp_log(rtp_log_level_t level, const char *fmt, ...);

#define LOG_DEBUG(...) rtp_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  rtp_log(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  rtp_log(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_ERR(...)   rtp_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_SUCCESS(...) rtp_log(LOG_LEVEL_SUCCESS, __VA_ARGS__)

#endif // RTP_LOG_H
