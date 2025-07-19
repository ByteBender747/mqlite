/**
 * @file utf8.c
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief Utility functions
 * @version 0.1
 * @date 2025-07-02
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

bool is_valid_utf8(const char *str, size_t length)
{
    size_t i = 0;

    while (i < length) {
        // Check first byte
        if (str[i] <= 0x7F) {
            // Single byte character (ASCII)
            i++;
            continue;
        }

        // Count number of leading 1s to determine byte count
        unsigned char first_byte = str[i];
        int byte_count = 0;

        if ((first_byte & 0xE0) == 0xC0) {
            byte_count = 2; // 110xxxxx
        } else if ((first_byte & 0xF0) == 0xE0) {
            byte_count = 3; // 1110xxxx
        } else if ((first_byte & 0xF8) == 0xF0) {
            byte_count = 4; // 11110xxx
        } else {
            return false; // Invalid first byte
        }

        // Check if we have enough bytes
        if (i + byte_count > length) {
            return false;
        }

        // Validate continuation bytes (should be 10xxxxxx)
        for (int j = 1; j < byte_count; j++) {
            if ((str[i + j] & 0xC0) != 0x80) {
                return false;
            }
        }

        // Check for overlong encodings
        switch (byte_count) {
        case 2:
            if ((first_byte & 0x1E) == 0) {
                return false;
            }
            break;
        case 3:
            if ((first_byte == 0xE0) && ((str[i + 1] & 0x20) == 0)) {
                return false;
            }
            break;
        case 4:
            if ((first_byte == 0xF0) && ((str[i + 1] & 0x30) == 0)) {
                return false;
            }
            break;
        default:
            break;
        }

        // Check for UTF-16 surrogate values (invalid in UTF-8)
        if (byte_count == 3) {
            unsigned int codepoint = ((first_byte & 0x0F) << 12) |
                                     ((str[i + 1] & 0x3F) << 6) |
                                     (str[i + 2] & 0x3F);
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                return false;
            }
        }

        // Check maximum valid UTF-8 codepoint
        if (byte_count == 4) {
            unsigned int codepoint =
                ((first_byte & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) |
                ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
            if (codepoint > 0x10FFFF) {
                return false;
            }
        }

        i += byte_count;
    }

    return true;
}