/**
 * @file utf8.h
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief Utility functions
 * @version 0.1
 * @date 2025-07-02
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

bool is_valid_utf8(const char *str, size_t length);
int dump(const void* p_data, size_t offset, size_t len, uint8_t wsize, uint8_t stride);

#endif /* UTILS_H_INCLUDED */