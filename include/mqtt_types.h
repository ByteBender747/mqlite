/**
 * @file mqtt_types.h
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief MQTT datatype definitions
 * @version 0.1
 * @date 2025-07-02
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef MQTT_TYPE_H_INCLUDED
#define MQTT_TYPE_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <memory.h>

#include "mqtt_const.h"

struct mqtt_client;

struct mqtt_pbuf {
    void* payload;
    size_t len;
};

struct mqtt_blob {
    uint8_t* data;
    uint16_t len;
    uint16_t maxlen;
};

struct mqtt_net_api {
    bool connected;
    int (*open_conn)(struct mqtt_client*, const char*);
    int (*close_conn)(struct mqtt_client*);
    int (*alloc_send_buf)(struct mqtt_client*, struct mqtt_pbuf*, uint32_t);
    int (*free_send_buf)(struct mqtt_client*, struct mqtt_pbuf*);
    int (*alloc_recv_buf)(struct mqtt_client*, struct mqtt_pbuf*, uint32_t);
    int (*free_recv_buf)(struct mqtt_client*, struct mqtt_pbuf*);
    int (*recv) (struct mqtt_client*, struct mqtt_pbuf*);
    int (*send) (struct mqtt_client*, struct mqtt_pbuf*);
};

struct mqtt_user_property {
    const char* key;
    const char* value;
};

struct mqtt_pub_packet {
    const char* topic;
    struct mqtt_blob payload;
    uint8_t qos;
    bool retain;
    bool dup;
    uint16_t packet_id;
};

struct mqtt_sub_entry {
    uint8_t qos;
    uint8_t no_local;
    uint8_t retain_as_published;
    uint8_t retain_handling;
    const char* topic;
};

struct mqtt_client {
    struct mqtt_net_api net;
    struct mqtt_pbuf outp;
    struct mqtt_pbuf inp;

    struct {
        bool un_flag;
        bool pw_flag;
        bool req_prob_inf;
        bool req_res_inf;
        bool will_retain;
        bool will_flag;
        bool clean_start;
        const char* will_topic;
        const char* will_data;
        uint8_t will_qos;
        uint16_t recv_max;
        uint16_t topic_alias_max;
        uint32_t max_packet_size;
        uint16_t keep_alive;
        uint32_t session_expiry_interval;
        struct {
            uint32_t delay_interval;
            uint8_t payload_format_indicator;
            uint32_t message_expiry_delay;
            const char* topic;
            const char* content_type;
            const char* response_topic;
            struct mqtt_blob correlation_data;
            struct mqtt_blob payload;
        } will;
        const char* user;
        const char* passwd;
        const char* client_id;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
        const char* auth_method;
        struct mqtt_blob auth_data;
    } connect;

    struct {
        uint16_t packet_id;
        uint8_t reason_code;
        const char* reason_string;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } pubrec;

    struct {
        uint16_t packet_id;
        uint8_t reason_code;
        const char* reason_string;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } pubrel;

    struct {
        uint16_t packet_id;
        uint8_t reason_code;
        const char* reason_string;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } pubcomp;

    struct {
        bool ack_flag;
        uint8_t reason;
        uint16_t recv_max;
        uint16_t topic_alias_max;
        uint8_t max_qos;
        uint32_t max_packet_size;
        bool retain_avail;
        char* assigned_client_id;
        char* reason_string;
        bool wildcard_sub_avail;
        bool sub_id_avail;
        bool shared_sub_avail;
        uint16_t server_keep_alive;
        char* server_reference;
        char* response_info;
    } connack;

    struct {
        const char* reason_string;
        const char* server_reference;
        uint8_t reason_code;
        uint32_t session_expiry_interval;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } disconn;

    struct {
        uint8_t payload_format_indicator;
        uint32_t message_expiry_interval;
        const char* content_type;
        const char* response_topic;
        struct mqtt_blob correlation_data;
        uint16_t topic_alias;
        uint32_t subscription_identifier;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } publish;

    struct {
        struct mqtt_sub_entry* entries;
        unsigned int entry_count;
        uint16_t packet_id;
        uint32_t subscription_identifier;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } subscribe;

    struct {
        struct mqtt_sub_entry* entries;
        unsigned int entry_count;
        uint16_t packet_id;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } unsubscribe;

    struct {
        uint16_t packet_id;
        uint8_t* reason_codes;
        unsigned int reason_codes_count;
        const char* reason_string;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } unsuback;

    struct {
        uint16_t packet_id;
        uint8_t reason_code;
        const char* reason_string;
        struct mqtt_user_property* user_properties;
        int user_properties_count;
    } puback;

    struct {
        uint16_t packet_id;
        mqtt_packet_type await_packet_type;
    } pending[MQTT_RECEIVE_MAXIMUM];

    struct {
        const char* topic;
        const char* response_topic;
        const char* content_type;
        struct mqtt_blob payload;
        struct mqtt_blob correlation_data;
        uint8_t correlation_data_buffer[MQTT_CORELATION_DATA_MAXIMUM];  // Buffer for correlation data
        uint16_t packet_id;
        uint32_t message_expiry_interval;
        uint32_t subscription_identifier;
        uint16_t topic_alias;
        uint8_t qos;
        uint8_t payload_format_indicator;
        bool dup;
        bool retain;
    } received_publish;

    void *context;
    char broker_addr[16];
    bool connected;
    uint8_t* pout;
    uint8_t* pin;
    uint32_t packet_size;
    uint16_t expected_ptypes;
    uint16_t packet_id_count;
    bool message_available;
};

#endif /* MQTT_TYPE_H_INCLUDED */