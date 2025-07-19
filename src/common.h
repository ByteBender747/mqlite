/**
 * @file common.h
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief Commonly used macro definitions
 * @version 0.8
 * @date 2023-06-19
 *
 * @copyright Copyright (c) 2021 - 2023
 *
 */

#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

/* Time value definitions */
#define SECOND        1
#define MINUTE        60
#define HOUR          3600
#define DAY           86400
#define WEEK          604800
#define YEAR          31536000

/* Millisecond base */
#define SECOND_MS     (SECOND*1000)
#define MINUTE_MS     (MINUTE*1000)
#define HOUR_MS       (HOUR*1000)
#define DAY_MS        (DAY*1000)
#define WEEK_MS       (WEEK*1000)

/* Microsecond base */
#define SECOND_US     (SECOND_MS*1000)
#define MINUTE_US     (MINUTE_MS*1000)
#define HOUR_US       (HOUR_MS*1000)
#define DAY_US        (DAY_MS*1000)
#define WEEK_US       (WEEK_MS*1000)

#define TIME(h, m, s)    ((h)*HOUR + (m)*MINUTE + (s)*SECOND)
#define TIME_MS(h, m, s) ((h)*HOUR_MS + (m)*MINUTE_MS + (s)*SECOND_MS)
#define TIME_US(h, m, s) ((h)*HOUR_US + (m)*MINUTE_US + (s)*SECOND_US)

/* Bit manipulation macros */
#define BIT(n) (1<<(n))
#define BITS(n) ((1<<(n))-1)
#define BIT_RANGE(x,n,p) ((((x)&((1<<(n))-1))<<(p)))
#define TST(n, mask) (((n) & (mask)) == (mask))
#define SET(n, mask) (n) |= (mask)
#define CLR(n, mask) (n) &= ~(mask)
#define TGL(n, mask) (n) ^= (mask)
#define CSET(n, con, mask) n = (con) ? ((n) | (mask)) : ((n) & ~(mask))
#define LO8(n)  ((n)  & 0x00FF)
#define HI8(n)  (((n) & 0xFF00) >> 8)
#define LO16(n) ((n)  & 0x0000FFFFL)
#define HI16(n) (((n) & 0xFFFF0000L) >> 16)
#define LO32(n) ((n)  & 0x00000000FFFFFFFFLL)
#define HI32(n) (((n) & 0xFFFFFFFF00000000LL) >> 32)

/* Numeric Conversions */
#define PRINTF_BINARY_PATTERN_INT8 "%c%c%c%c%c%c%c%c"
#define PRINTF_BYTE_TO_BINARY_INT8(i) \
    (((i)&0x80ll) ? '1' : '0'),       \
        (((i)&0x40ll) ? '1' : '0'),   \
        (((i)&0x20ll) ? '1' : '0'),   \
        (((i)&0x10ll) ? '1' : '0'),   \
        (((i)&0x08ll) ? '1' : '0'),   \
        (((i)&0x04ll) ? '1' : '0'),   \
        (((i)&0x02ll) ? '1' : '0'),   \
        (((i)&0x01ll) ? '1' : '0')

#define PRINTF_BINARY_PATTERN_INT16 \
    PRINTF_BINARY_PATTERN_INT8 PRINTF_BINARY_PATTERN_INT8
#define PRINTF_BYTE_TO_BINARY_INT16(i) \
    PRINTF_BYTE_TO_BINARY_INT8((i) >> 8), PRINTF_BYTE_TO_BINARY_INT8(i)
#define PRINTF_BINARY_PATTERN_INT32 \
    PRINTF_BINARY_PATTERN_INT16 PRINTF_BINARY_PATTERN_INT16
#define PRINTF_BYTE_TO_BINARY_INT32(i) \
    PRINTF_BYTE_TO_BINARY_INT16((i) >> 16), PRINTF_BYTE_TO_BINARY_INT16(i)
#define PRINTF_BINARY_PATTERN_INT64 \
    PRINTF_BINARY_PATTERN_INT32 PRINTF_BINARY_PATTERN_INT32
#define PRINTF_BYTE_TO_BINARY_INT64(i) \
    PRINTF_BYTE_TO_BINARY_INT32((i) >> 32), PRINTF_BYTE_TO_BINARY_INT32(i)

#define IS_DEC_CHAR(c) (IS_IN_RANGE(c, '0', '9'))
#define CHAR_TO_DEC(c) (IS_DEC_CHAR(c) ? ((c) - '0') : -1)

#define IS_HEX_CHAR(c) (IS_IN_RANGE(c, '0', '9') || IS_IN_RANGE(c, 'a', 'f') || IS_IN_RANGE(c, 'A', 'F'))
#define CHAR_TO_HEX(c) (IS_IN_RANGE(c, '0', '9') ? ((c) - '0') : IS_IN_RANGE(c, 'a', 'f') ? ((c) - 'a' + 10) \
        : IS_IN_RANGE(c, 'A', 'F')                                                        ? ((c) - 'A' + 10) \
                                                                                          : -1)

/* Numeric macros */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define IS_IN_RANGE(a, b, c) (((a) >= (b)) && ((a) <= (c)))
#define INTO_RANGE(x, a, b) (((x) < (a)) ? (a) : ((x) > (b)) ? (b) \
                                                             : (x))

/* Misc */
#define ARRAY_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Attributes */
#define WEAK    __attribute__((weak))
#define OS_MAIN __attribute__((OS_main))
#define OS_TASK __attribute__((OS_task))

#endif /* COMMON_H_INCLUDED */
