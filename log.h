#ifndef ALLOG_H
#define ALLOG_H

#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AsiLoader", __VA_ARGS__)
#define VERBOSE(...) __android_log_print(ANDROID_LOG_VERBOSE, "AsiLoader", __VA_ARGS__)
#define WARN(...) __android_log_print(ANDROID_LOG_WARN, "AsiLoader", __VA_ARGS__)
#define ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "AsiLoader", __VA_ARGS__)
#define FATAL(...) __android_log_print(ANDROID_LOG_FATAL, "AsiLoader", __VA_ARGS__)

#endif // ALLOG_H
