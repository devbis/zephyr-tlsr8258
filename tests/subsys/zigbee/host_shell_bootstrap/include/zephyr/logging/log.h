#ifndef ZEPHYR_LOGGING_LOG_H_
#define ZEPHYR_LOGGING_LOG_H_

static inline void host_log_ignore(const char *fmt, ...)
{
	(void)fmt;
}

#define LOG_MODULE_REGISTER(...) /* nothing */
#define LOG_INF(...) host_log_ignore(__VA_ARGS__)
#define LOG_WRN(...) host_log_ignore(__VA_ARGS__)
#define LOG_ERR(...) host_log_ignore(__VA_ARGS__)

#endif
