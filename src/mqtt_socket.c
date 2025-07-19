/**
 * @file mqtt_socket.c
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief MQTT Network interface implementation for Berkeley sockets
 * @version 0.1
 * @date 2025-06-30
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

#include "mqtt_types.h"
#include "status.h"

#define RECV_BUFFER_SIZE      4096

struct socket_context {
    int handle;
};

static int create_tcp_client(const char* ipaddr, uint16_t port, int* sh)
{
    struct sockaddr_in sa;
    int handle = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == -1) {
        return ERROR_HW_FAILURE;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (!ipaddr) {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        int res = inet_pton(AF_INET, ipaddr, &sa.sin_addr);
        if (res == -1) {
            return ERROR_INVALID_DATA;
        }
    }
    if (connect(handle, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        close(handle);
        return ERROR_HOST_UNAVAILABLE;
    }
    *sh = handle;
    return OK;
}

static int alloc_send_buf(struct mqtt_client* client, struct mqtt_pbuf* buf, uint32_t len)
{
    buf->payload = malloc(len);
    if (!buf->payload) {
        buf->len = 0;
        return ERROR_OUT_OF_MEMORY;
    }
    buf->len = len;
    return OK;
}

static int alloc_recv_buf(struct mqtt_client* client, struct mqtt_pbuf* buf, uint32_t len)
{
    uint32_t alloc_len = (len > 0) ? len : RECV_BUFFER_SIZE;
    buf->payload = malloc(alloc_len);
    if (!buf->payload) {
        buf->len = 0;
        return ERROR_OUT_OF_MEMORY;
    }
    buf->len = alloc_len;
    return OK;
}

static int free_send_buf(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (buf->payload) {
        free(buf->payload);
        buf->payload = NULL;
    }
    buf->len = 0;
    return OK;
}

static int free_recv_buf(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (buf->payload) {
        free(buf->payload);
        buf->payload = NULL;
    }
    buf->len = 0;
    return OK;
}

static int open_conn(struct mqtt_client* client, const char* addr)
{
    if (!client || !addr || !client->context) {
        return ERROR_NULL_REFERENCE;
    }
    struct socket_context* ctx = (struct socket_context*)client->context;
    int result = create_tcp_client(addr, MQTT_PORT, &ctx->handle);
    return result;
}

static int close_conn(struct mqtt_client* client)
{
    if (!client || !client->context) {
        return ERROR_NULL_REFERENCE;
    }
    
    struct socket_context* ctx = (struct socket_context*) client->context;
    if (close(ctx->handle) == -1) {
        return ERROR_HW_FAILURE;
    }
    return OK;
}

static int socket_send(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (!client || !buf || !buf->payload || !client->context) {
        return ERROR_NULL_REFERENCE;
    }
    
    struct socket_context* ctx = (struct socket_context*) client->context;
    ssize_t bytes_sent = send(ctx->handle, buf->payload, buf->len, 0);
    
    if (bytes_sent == -1) {
        switch (errno) {
            case ECONNRESET:
            case EPIPE:
                return ERROR_HOST_UNAVAILABLE;
            case EAGAIN:
                return STATUS_BUSY;
            default:
                return ERROR_HW_FAILURE;
        }
    }
    
    if ((size_t)bytes_sent != buf->len) {
        return ERROR_INVALID_DATA;  // Partial send
    }
    
    return OK;
}

static int socket_recv(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (!client || !buf || !buf->payload) {
        return ERROR_NULL_REFERENCE;
    }
    
    struct socket_context* ctx = (struct socket_context*) client->context;
    struct pollfd pfd = { .events = POLLIN, .fd = ctx->handle };

    // Poll socket and check if there are data available for read
    poll(&pfd, 1, MQTT_POLL_TIMEOUT);

    if ((pfd.revents & POLLIN) == POLLIN) {
        ssize_t bytes_received = recv(ctx->handle, buf->payload, buf->len, 0);
        if (bytes_received == -1) {
            buf->len = 0;
            switch (errno) {
                case ECONNRESET:
                    return ERROR_HOST_UNAVAILABLE;
                case EAGAIN:
                    return STATUS_BUSY;
                default:
                    return ERROR_HW_FAILURE;
            }
        }
        
        if (bytes_received == 0) {
            buf->len = 0;
            return ERROR_HOST_UNAVAILABLE;  // Connection closed by peer
        }
        
        buf->len = (uint32_t)bytes_received;
    } else {
        return STATUS_PASSED;
    }

    return STATUS_SUCCESS;
}

void mqtt_assign_net_api(struct mqtt_client* client)
{
    static struct socket_context ctx;
    client->net.alloc_recv_buf = alloc_recv_buf;
    client->net.alloc_send_buf = alloc_send_buf;
    client->net.free_recv_buf = free_recv_buf;
    client->net.free_send_buf = free_send_buf;
    client->net.open_conn = open_conn;
    client->net.close_conn = close_conn;
    client->net.send = socket_send;
    client->net.recv = socket_recv;
    client->context = &ctx;
}