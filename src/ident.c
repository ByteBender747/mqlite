/**
 * @file indent.c
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief Creation of unique client identifier
 * @version 0.1
 * @date 2025-06-30
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>

#ifdef _WIN32
#include <windows.h>
#else
#ifdef PICO_BOARD
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif
#endif

#define MAX_HOSTNAME_LEN      256
#define NAX_UNIQUE_ID_LEN     (MAX_HOSTNAME_LEN + 32)

static const char* client_id_prefix = "MQLite";

static char* assign_string(const char* input)
{
    char* string = (char*) malloc(strlen(input)+1);
    strcpy(string, input);
    return string;
}

char *get_unique_client_id()
{
    uint64_t ticks = 0;
    char id[NAX_UNIQUE_ID_LEN];
    char hostname[MAX_HOSTNAME_LEN];
    uint32_t hostname_len = MAX_HOSTNAME_LEN;

    #ifdef _WIN32
        if (!GetComputerNameA(hostname, &hostname_len)) {
            return NULL;
        }
        
        // GetTickCount64 returns milliseconds since system start
        ticks = GetTickCount64() / 1000; // Convert to seconds
    #else
    #ifdef PICO_BOARD
        pico_get_unique_board_id_string(hostname, sizeof(hostname));
        ticks = get_absolute_time();
    #else
        struct sysinfo info;
        if (gethostname(hostname, MAX_HOSTNAME_LEN) != 0) {
            return NULL;
        }

        if (sysinfo(&info) != 0) {
            return NULL;
        }
        ticks = info.uptime;
    #endif
    #endif

    // Combine hostname and uptime to create a unique ID
    (void)snprintf(id, NAX_UNIQUE_ID_LEN, "%s@%s_%llu",
             client_id_prefix, hostname, ticks);

    return assign_string(id);
}