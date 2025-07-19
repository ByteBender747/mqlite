/**
 * @file logging.h
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief Logging macros
 * @version 0.1
 * @date 2025-07-13
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef LOGGING_H_INCLUDED
#define LOGGING_H_INCLUDED

#include <stdio.h>

#ifdef NDEBUG
#define LOG_PRINTF(level, format, ...) \
    printf("[%s]: " format "\n", level , ##__VA_ARGS__)
#else
#define LOG_PRINTF(level, format, ...) \
    printf("[%s](%s:%d): " format "\n", level, __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#define LOG_INFO(format, ...)  LOG_PRINTF("INF", format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG_PRINTF("ERR", format, ##__VA_ARGS__)

#ifndef NDEBUG
#define LOG_DEBUG(format, ...) LOG_PRINTF("DBG", format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(format, ...)
#endif

#endif /* LOGGING_H_INCLUDED */