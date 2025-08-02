/**
 * @file mqtt_lwip.c
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief MQLite network stack driver for lwIP
 * @version 0.1
 * @date 2025-07-12
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "status.h"
#include "mqtt.h"
#include "logging.h"
#include <lwip/err.h>
#include <stdbool.h>

#define MAX_BUFFER_LEN  4096

struct socket_context {
    struct tcp_pcb *tcp_pcb;
    struct mqtt_client *client;
    ip_addr_t remote_addr;
    char buffer[MAX_BUFFER_LEN];
    uint16_t send_len;
    bool connecting;
    int mqtt_proc_state;
};

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    struct socket_context* state = (struct socket_context*) arg;
    if (err != ERR_OK) {
        LOG_ERROR("LwIP: error: %d", err);
        return err;
    }
    if (state) {
        state->client->net.connected = true;
        state->connecting = false;
        LOG_DEBUG("LwIP: TCP connected");
        if (state->client->connect.deferred) {
            int result = state->client->net.send(state->client, &state->client->outp);
            state->client->net.free_send_buf(state->client, &state->client->outp);
            if (FAILED(result)) {
                state->client->net.close_conn(state->client);
                return ERR_CONN;
            }
            state->client->connect.deferred = false;
        }
    } else {
        LOG_ERROR("LwIP: missing arg!");
        return -1;
    }
    return ERR_OK;
}

static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct socket_context* state = (struct socket_context*) arg;
    if (!p) {
        LOG_ERROR("LwIP: missing arg reference!");
        return -1;
    }

    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
#ifdef MQTT_LWIP_VERBOSE
        LOG_DEBUG("LwIP: tcp recv data=0x%p, len=%d", p->payload, p->len);
#endif
        state->mqtt_proc_state = mqtt_process_packet(state->client, p->payload, p->tot_len);
        if (FAILED(state->mqtt_proc_state)) {
            LOG_ERROR("mqtt_process_packet() returned: %d", state->mqtt_proc_state);
        }
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;
}

static void tcp_client_err(void *arg, err_t err)
{
    if (err != ERR_ABRT) {
        LOG_ERROR("LwIP: tcp_client_err: %d", err);
    }
}

static int alloc_send_buf(struct mqtt_client* client, struct mqtt_pbuf* buf, uint32_t len)
{
    int res = STATUS_SUCCESS;
    if (client) {
        struct socket_context* state = (struct socket_context*) client->context;
        if (!state) {
            return ERROR_NULL_REFERENCE;
        }
        if (len <= MAX_BUFFER_LEN) {
            buf->payload = state->buffer;
            buf->len = len;
        } else {
            buf->payload = NULL;
            buf->len = 0;
            res = ERROR_OUT_OF_MEMORY;
        }
    } else {
        res = ERROR_NULL_REFERENCE;
    }
    return res;
}

static int free_send_buf(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (buf) {
        buf->payload = NULL;
        buf->len = 0;
        return STATUS_SUCCESS;
    }
    return ERROR_NULL_REFERENCE;
}

static int open_conn(struct mqtt_client* client, const char* addr)
{
    int result = STATUS_SUCCESS;
    struct socket_context* ctx = NULL;

    if (!client) {
        return ERROR_NULL_REFERENCE;
    }
    if (!addr) {
        return ERROR_NULL_REFERENCE;
    }
    ctx = (struct socket_context*) client->context;
    if (!ctx) {
        return ERROR_NULL_REFERENCE;
    }

    if (!ctx->client->net.connected && !ctx->connecting) {
        ip4addr_aton(addr, &ctx->remote_addr);
        if (!ctx->tcp_pcb) {
            ctx->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&ctx.remote_addr));
            if (!ctx->tcp_pcb) {
                return ERROR_SW_FAILURE;
            }
        }
        tcp_arg(ctx->tcp_pcb, ctx);
        tcp_recv(ctx->tcp_pcb, tcp_client_recv);
        tcp_err(ctx->tcp_pcb, tcp_client_err);
        LOG_INFO("LwIP: connecting to host %s", addr);
        cyw43_arch_lwip_begin();
        err_t err = tcp_connect(ctx->tcp_pcb, &ctx->remote_addr, MQTT_PORT, tcp_client_connected);
        if (err != ERR_OK) {
            LOG_ERROR("LwIP: tcp_connect() returned: %d", err);
            result = ERROR_HOST_UNAVAILABLE;
        } else {
            ctx->connecting = true;
        }
        cyw43_arch_lwip_end();
    }

    return result;
}

static int close_conn(struct mqtt_client* client)
{
    struct socket_context* state = NULL;
    if (client) {
        state = (struct socket_context*) client->context;
        if (!state) {
            return ERROR_NULL_REFERENCE;
        }
    } else {
        return ERROR_NULL_REFERENCE;
    }

    if (state->client->net.connected) {
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        tcp_arg(state->tcp_pcb, NULL);
        tcp_close(state->tcp_pcb);
        state->client->net.connected = false;
        state->tcp_pcb = NULL;
        LOG_DEBUG("LwIP: TCP connection closed");
    }

    return OK;
}

static int socket_send(struct mqtt_client* client, struct mqtt_pbuf* buf)
{
    if (client) {
        struct socket_context* state = (struct socket_context*) client->context;
        if (state) {
            if (!state->client->net.connected)  {
                return state->client->connect.deferred ? STATUS_PENDING : ERROR_NOT_CONNECTED;
            }
            cyw43_arch_lwip_begin();
#ifdef MQTT_LWIP_VERBOSE
            LOG_DEBUG("LwIP: tcp send data=0x%p, len=%d", buf->payload, buf->len);
#endif
            err_t err = tcp_write(state->tcp_pcb, buf->payload, buf->len, TCP_WRITE_FLAG_COPY);
            if (err != ERR_OK) {
                LOG_ERROR("LwIP: tcp_write() returned: %d", err);
                return ERROR_SW_FAILURE;
            }
            cyw43_arch_lwip_end();
            return STATUS_SUCCESS;
        }
    }
    return ERROR_NULL_REFERENCE;
}

void mqtt_assign_net_api(struct mqtt_client* client)
{
    static struct socket_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (client) {
        client->net.alloc_send_buf = alloc_send_buf;
        client->net.free_send_buf = free_send_buf;
        client->net.open_conn = open_conn;
        client->net.close_conn = close_conn;
        client->net.send = socket_send;
        client->context = &ctx;
        ctx.client = client;
    }
}