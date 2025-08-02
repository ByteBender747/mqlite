/**
 * @file mqtt_client.c
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief MQTT Protocol client functionality
 * @version 0.1
 * @date 2025-06-30
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "mqtt.h"
#include "common.h"
#include "status.h"
#include "utf8.h"

#define STRLEN(s)  ((s) ? strlen(s) + 2 : 2)
#define BLEN(b)    ((b).len + 2)

/* This function must be implemented by the network interface module! */
void mqtt_assign_net_api(struct mqtt_client* stat);

/* From indent module */
char *get_unique_client_id();

/* Only internally used */
int mqtt_puback(struct mqtt_client* stat, uint16_t packet_id);
int mqtt_pubrec(struct mqtt_client* stat, uint16_t packet_id);
int mqtt_pubrel(struct mqtt_client* stat, uint16_t packet_id);
int mqtt_pubcomp(struct mqtt_client* stat, uint16_t packet_id);

/***** Data pack/unpacking ***********************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static inline void pack_byte(struct mqtt_client* stat, uint8_t data)
{
    *(stat->pout++) = data;
}

static inline void pack_word(struct mqtt_client* stat, uint16_t data)
{
    pack_byte(stat, data >> 8);
    pack_byte(stat, data);
}

static inline void pack_dword(struct mqtt_client* stat, uint32_t data)
{
    pack_byte(stat, data >> 24);
    pack_byte(stat, data >> 16);
    pack_byte(stat, data >> 8);
    pack_byte(stat, data);
}

static void pack_binary(struct mqtt_client *stat, const struct mqtt_blob *blob)
{
    uint16_t len = blob->len;
    pack_word(stat, len);
    if (blob->data) {
        for (uint16_t i = 0; i < len; ++i) {
            pack_byte(stat, blob->data[i]);
        }
    }
}

static inline uint8_t unpack_byte(struct mqtt_client* stat)
{
    return *(stat->pin++);
}

static inline uint16_t unpack_word(struct mqtt_client* stat)
{
    uint16_t data = unpack_byte(stat) << 8;
    data |= unpack_byte(stat);
    return data;
}

static inline uint32_t unpack_dword(struct mqtt_client* stat)
{
    uint32_t data = unpack_byte(stat) << 24;
    data |= unpack_byte(stat) << 16;
    data |= unpack_byte(stat) << 8;
    data |= unpack_byte(stat);
    return data;
}

static int unpack_binary(struct mqtt_client *stat, struct mqtt_blob *blob)
{
    uint16_t length = unpack_word(stat);
    if (length > blob->maxlen) {
        return ERROR_INDEX_OUT_OF_RANGE;
    }
    blob->len = length;
    for (size_t i = 0; i < length; ++i) {
        blob->data[i] = unpack_byte(stat);
    }
    return OK;
}

static void pack_string(struct mqtt_client* stat, const char* data)
{
    uint16_t len = strlen(data);
    pack_word(stat, len);
    for (uint16_t i = 0; i < len; ++i) {
        pack_byte(stat, data[i]);
    }
}

static char* unpack_string(struct mqtt_client* stat)
{
    char* string = NULL;
    int len = unpack_word(stat);
    if (len > 0) {
        string = malloc(len + 1);
        assert(string);
        for (int i = 0; i < len; ++i) {
            string[i] = (char) unpack_byte(stat);
        }
        string[len] = '\0';
    }
    return string;
}

static char* string_copy(const char* source)
{
    if (source == NULL) {
        return NULL;
    }

    size_t len = strlen(source);
    char* copy = malloc(len + 1);

    if (copy == NULL) {
        return NULL; // Memory allocation failed
    }

    // Copy the string to the allocated memory
    strcpy(copy, source);

    return copy;
}

static void pack_variable_size(struct mqtt_client* stat, uint32_t size)
{
    do {
        uint8_t encoded = size & 0x7f; /* size MOD 128 */
        size = size >> 7; /* size / 128 */
        if (size > 0) {
            encoded = encoded | 0x80;
        }
        pack_byte(stat, encoded);

    } while (size > 0);
}

static uint32_t unpack_variable_size(struct mqtt_client* stat)
{
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t encoded_byte = 0;
    int shift = 0;

    do {
        uint8_t encoded_byte = unpack_byte(stat);
        value += (encoded_byte & 0x7F) * multiplier;
        multiplier <<= 7;
        shift++;
    } while ((encoded_byte & 0x80) != 0 && shift < 4);

    return value;
}

static inline uint8_t get_variable_size_byte_count(uint32_t len)
{
    if (len <= 127) {
        return 1;
    }
    if (len <= 16383) {
        return 2;
    }
    if (len <= 2097151) {
        return 3;
    }
    return 4;
}

static void write_fixed_header(struct mqtt_client* stat, mqtt_packet_type type, uint8_t flags, uint32_t len)
{
    pack_byte(stat, ((uint8_t)type & 0x0f) << 4 | (flags & 0x0f));
    pack_variable_size(stat, len);
}

/***** Weak functions ****************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

void WEAK mqtt_user_property(struct mqtt_client* stat, mqtt_packet_type origin, const char* key, const char* value)
{
    /* No user property handling by default */
}

void WEAK mqtt_received_publish(struct mqtt_client* stat)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_subscription_declined(struct mqtt_client* stat, uint16_t packet_id, int num, uint8_t reason_code)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_subscription_granted(struct mqtt_client* stat, uint16_t packet_id, int num)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_received_disconnect(struct mqtt_client* stat, mqtt_reason_code reason_code)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_ping_received(struct mqtt_client* stat)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_publish_acknowledged(struct mqtt_client* stat, uint16_t packet_id, uint8_t reason_code)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_publish_completed(struct mqtt_client* stat, uint16_t packet_id, uint8_t reason_code)
{
    /* Can be overloaded by user code */
}

void WEAK mqtt_connected(struct mqtt_client* stat)
{
    /* Can be overloaded by user code */
}

/***** Packet size estimations *******************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static inline uint32_t estimate_fixed_header_size(uint32_t len)
{
    return get_variable_size_byte_count(len) + 1;
}

static uint32_t estimate_conn_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->connect.session_expiry_interval) {
        size += 5;  // 1 byte ID + 4 bytes value
    }
    if (stat->connect.recv_max) {
        size += 3;  // 1 byte ID + 2 bytes value
    }
    if (stat->connect.max_packet_size) {
        size += 5;  // 1 byte ID + 4 bytes value
    }
    if (stat->connect.topic_alias_max) {
        size += 3;  // 1 byte ID + 2 bytes value
    }
    if (stat->connect.req_res_inf) {
        size += 2;  // 1 byte ID + 1 byte value
    }
    if (stat->connect.req_prob_inf) {
        size += 2;  // 1 byte ID + 1 byte value
    }
    if (stat->connect.auth_method) {
        size += 1 + STRLEN(stat->connect.auth_method);  // 1 byte ID + string length
    }
    if (stat->connect.auth_data.data && stat->connect.auth_data.len) {
        size += 1 + stat->connect.auth_data.len;  // 1 byte ID + binary data length
    }
    for (int i = 0; i < stat->connect.user_properties_count; i++) {
        size += STRLEN(stat->connect.user_properties[i].key);
        size += STRLEN(stat->connect.user_properties[i].value) + 1;
    }
    return size;
}

static uint32_t estimate_will_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->connect.will.delay_interval) {
        size += 5;
    }
    if (stat->connect.will.payload_format_indicator) {
        size += 2;
    }
    if (stat->connect.will.message_expiry_delay) {
        size += 5;
    }
    if (stat->connect.will.content_type) {
        size += STRLEN(stat->connect.will.content_type) + 1;
    }
    if (stat->connect.will.response_topic) {
        size += STRLEN(stat->connect.will.response_topic) + 1;
    }
    if (stat->connect.will.correlation_data.len) {
        size += BLEN(stat->connect.will.correlation_data) + 1;
    }
    return size;
}

static uint32_t estimate_disconn_prop_size(struct mqtt_client *stat)
{
    uint32_t size = 0;
    if (stat->disconn.session_expiry_interval) {
        size += 5;
    }
    if (stat->disconn.reason_string) {
        size += STRLEN(stat->disconn.reason_string) + 1;
    }
    if (stat->disconn.server_reference) {
        size += STRLEN(stat->disconn.server_reference) + 1;
    }
    for (int i = 0; i < stat->disconn.user_properties_count; i++) {
        size += STRLEN(stat->disconn.user_properties[i].key);
        size += STRLEN(stat->disconn.user_properties[i].value) + 1;
    }
    return size;
}

static uint32_t estimate_publish_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->publish.payload_format_indicator) {
        size += 2;  // 1 byte ID + 1 byte value
    }
    if (stat->publish.message_expiry_interval) {
        size += 5;  // 1 byte ID + 4 bytes value
    }
    if (stat->publish.content_type) {
        size += 1 + STRLEN(stat->publish.content_type);  // 1 byte ID + string length
    }
    if (stat->publish.response_topic) {
        size += 1 + STRLEN(stat->publish.response_topic);  // 1 byte ID + string length
    }
    if (stat->publish.correlation_data.len) {
        size += 1 + BLEN(stat->publish.correlation_data);  // 1 byte ID + binary data length
    }
    if (stat->publish.topic_alias) {
        size += 3;  // 1 byte ID + 2 bytes value
    }
    if (stat->publish.subscription_identifier) {
        size += 1 + get_variable_size_byte_count(stat->publish.subscription_identifier);  // 1 byte ID + variable int
    }
    for (int i = 0; i < stat->publish.user_properties_count; i++) {
        size += 1 + STRLEN(stat->publish.user_properties[i].key);
        size += STRLEN(stat->publish.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_subscribe_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->subscribe.subscription_identifier) {
        size += 1 + get_variable_size_byte_count(stat->subscribe.subscription_identifier);
    }
    for (int i = 0; i < stat->subscribe.user_properties_count; i++) {
        size += 1 + STRLEN(stat->subscribe.user_properties[i].key);
        size += STRLEN(stat->subscribe.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_puback_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->puback.reason_string) {
        size += 1 + STRLEN(stat->puback.reason_string);  // 1 byte ID + string length
    }
    for (int i = 0; i < stat->puback.user_properties_count; i++) {
        size += 1 + STRLEN(stat->puback.user_properties[i].key);
        size += STRLEN(stat->puback.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_pubrec_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->pubrec.reason_string) {
        size += 1 + STRLEN(stat->pubrec.reason_string);  // 1 byte ID + string length
    }
    for (int i = 0; i < stat->pubrec.user_properties_count; i++) {
        size += 1 + STRLEN(stat->pubrec.user_properties[i].key);
        size += STRLEN(stat->pubrec.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_pubrel_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->pubrel.reason_string) {
        size += 1 + STRLEN(stat->pubrel.reason_string);  // 1 byte ID + string length
    }
    for (int i = 0; i < stat->pubrel.user_properties_count; i++) {
        size += 1 + STRLEN(stat->pubrel.user_properties[i].key);
        size += STRLEN(stat->pubrel.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_pubcomp_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    if (stat->pubcomp.reason_string) {
        size += 1 + STRLEN(stat->pubcomp.reason_string);  // 1 byte ID + string length
    }
    for (int i = 0; i < stat->pubcomp.user_properties_count; i++) {
        size += 1 + STRLEN(stat->pubcomp.user_properties[i].key);
        size += STRLEN(stat->pubcomp.user_properties[i].value);
    }
    return size;
}

static uint32_t estimate_unsubscribe_prop_size(struct mqtt_client* stat)
{
    uint32_t size = 0;
    for (int i = 0; i < stat->unsubscribe.user_properties_count; i++) {
        size += 1 + STRLEN(stat->unsubscribe.user_properties[i].key);
        size += STRLEN(stat->unsubscribe.user_properties[i].value);
    }
    return size;
}

/***** Packet property generation ****************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static void pack_subscribe_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_subscribe_prop_size(stat));
    if (stat->subscribe.subscription_identifier) {
        pack_byte(stat, MQTT_SUB_SUBSCRIPTION_IDENTIFIER_ID);
        pack_variable_size(stat, stat->subscribe.subscription_identifier);
    }
    for (int i = 0; i < stat->subscribe.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->subscribe.user_properties[i].key);
        pack_string(stat, stat->subscribe.user_properties[i].value);
    }
}

static void pack_conn_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_conn_prop_size(stat));
    if (stat->connect.session_expiry_interval) {
        pack_byte(stat, MQTT_CON_SESSION_EXPIRY_INTERVAL_ID);
        pack_dword(stat, stat->connect.session_expiry_interval);
    }
    if (stat->connect.recv_max) {
        pack_byte(stat, MQTT_CON_RECEIVE_MAXIMUM_ID);
        pack_word(stat, stat->connect.recv_max);
    }
    if (stat->connect.max_packet_size) {
        pack_byte(stat, MQTT_CON_MAXIMUM_PACKET_SIZE_ID);
        pack_dword(stat, stat->connect.max_packet_size);
    }
    if (stat->connect.topic_alias_max) {
        pack_byte(stat, MQTT_CON_TOPIC_ALIAS_MAXIMUM_ID);
        pack_word(stat, stat->connect.topic_alias_max);
    }
    if (stat->connect.req_res_inf) {
        pack_byte(stat, MQTT_CON_REQUEST_RESPONSE_INFO_ID);
        pack_byte(stat, stat->connect.req_res_inf & 0x01);
    }
    if (stat->connect.req_prob_inf) {
        pack_byte(stat, MQTT_CON_REQUEST_PROBLEM_INF_ID);
        pack_byte(stat, stat->connect.req_prob_inf & 0x01);
    }
    if (stat->connect.auth_method) {
        pack_byte(stat, MQTT_CON_AUTH_METHOD_ID);
        pack_string(stat, stat->connect.auth_method);
    }
    if (stat->connect.auth_data.data && stat->connect.auth_data.len) {
        pack_byte(stat, MQTT_CON_AUTH_DATA_ID);
        pack_binary(stat, &stat->connect.auth_data);
    }
}

static void pack_will_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_will_prop_size(stat));
    if (stat->connect.will.delay_interval) {
        pack_byte(stat, MQTT_WILL_DELAY_INTERVAL_ID);
        pack_dword(stat, stat->connect.will.delay_interval);
    }
    if (stat->connect.will.payload_format_indicator) {
        pack_byte(stat, MQTT_WILL_FORMAT_INDICATOR_ID);
        pack_byte(stat, stat->connect.will.payload_format_indicator);
    }
    if (stat->connect.will.message_expiry_delay) {
        pack_byte(stat, MQTT_WILL_MSG_EXPIRY_INTERVAL_ID);
        pack_dword(stat, stat->connect.will.message_expiry_delay);
    }
    if (stat->connect.will.content_type) {
        pack_byte(stat, MQTT_WILL_CONTENT_TYPE_ID);
        pack_string(stat, stat->connect.will.content_type);
    }
    if (stat->connect.will.response_topic) {
        pack_byte(stat, MQTT_WILL_RESPONSE_TOPIC_ID);
        pack_string(stat, stat->connect.will.response_topic);
    }
    if (stat->connect.will.correlation_data.len) {
        pack_byte(stat, MQTT_WILL_CORELATION_DATA_ID);
        pack_binary(stat, &stat->connect.will.correlation_data);
    }
}

static void pack_disconn_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_will_prop_size(stat));
    if (stat->disconn.session_expiry_interval) {
        pack_byte(stat, MQTT_DISC_SESSION_EXPIRY_INTERVAL_ID);
        pack_dword(stat, stat->disconn.session_expiry_interval);
    }
    if (stat->disconn.reason_string) {
        pack_byte(stat, MQTT_DISC_REASON_STRING_ID);
        pack_string(stat, stat->disconn.reason_string);
    }
    if (stat->disconn.server_reference) {
        pack_byte(stat, MQTT_DISC_SERVER_REFERENCE_ID);
        pack_string(stat, stat->disconn.server_reference);
    }
    for (int i = 0; i < stat->disconn.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->disconn.user_properties[i].key);
        pack_string(stat, stat->disconn.user_properties[i].value);
    }
}

static void pack_publish_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_publish_prop_size(stat));
    if (stat->publish.payload_format_indicator) {
        pack_byte(stat, MQTT_PUB_PAYLOAD_FORMAT_INDICATOR_ID);
        pack_byte(stat, stat->publish.payload_format_indicator);
    }
    if (stat->publish.message_expiry_interval) {
        pack_byte(stat, MQTT_PUB_MESSAGE_EXPIRY_INTERVAL_ID);
        pack_dword(stat, stat->publish.message_expiry_interval);
    }
    if (stat->publish.content_type) {
        pack_byte(stat, MQTT_PUB_CONTENT_TYPE_ID);
        pack_string(stat, stat->publish.content_type);
    }
    if (stat->publish.response_topic) {
        pack_byte(stat, MQTT_PUB_RESPONSE_TOPIC_ID);
        pack_string(stat, stat->publish.response_topic);
    }
    if (stat->publish.correlation_data.len) {
        pack_byte(stat, MQTT_PUB_CORRELATION_DATA_ID);
        pack_binary(stat, &stat->publish.correlation_data);
    }
    if (stat->publish.topic_alias) {
        pack_byte(stat, MQTT_PUB_TOPIC_ALIAS_ID);
        pack_word(stat, stat->publish.topic_alias);
    }
    if (stat->publish.subscription_identifier) {
        pack_byte(stat, MQTT_PUB_SUBSCRIPTION_IDENTIFIER_ID);
        pack_variable_size(stat, stat->publish.subscription_identifier);
    }
    for (int i = 0; i < stat->publish.user_properties_count; i++) {
        pack_byte(stat, MQTT_PUB_USER_PROPERTY_ID);
        pack_string(stat, stat->publish.user_properties[i].key);
        pack_string(stat, stat->publish.user_properties[i].value);
    }
}

static void pack_puback_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_puback_prop_size(stat));
    if (stat->puback.reason_string) {
        pack_byte(stat, MQTT_PUBACK_REASON_STRING_ID);
        pack_string(stat, stat->puback.reason_string);
    }
    for (int i = 0; i < stat->puback.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->puback.user_properties[i].key);
        pack_string(stat, stat->puback.user_properties[i].value);
    }
}

static void pack_pubrec_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_pubrec_prop_size(stat));
    if (stat->pubrec.reason_string) {
        pack_byte(stat, MQTT_PUBREC_REASON_STRING_ID);
        pack_string(stat, stat->pubrec.reason_string);
    }
    for (int i = 0; i < stat->pubrec.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->pubrec.user_properties[i].key);
        pack_string(stat, stat->pubrec.user_properties[i].value);
    }
}

static void pack_pubrel_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_pubrel_prop_size(stat));
    if (stat->pubrel.reason_string) {
        pack_byte(stat, MQTT_PUBREL_REASON_STRING_ID);
        pack_string(stat, stat->pubrel.reason_string);
    }
    for (int i = 0; i < stat->pubrel.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->pubrel.user_properties[i].key);
        pack_string(stat, stat->pubrel.user_properties[i].value);
    }
}

static void pack_pubcomp_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_pubcomp_prop_size(stat));
    if (stat->pubcomp.reason_string) {
        pack_byte(stat, MQTT_PUBCOMP_REASON_STRING_ID);
        pack_string(stat, stat->pubcomp.reason_string);
    }
    for (int i = 0; i < stat->pubcomp.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->pubcomp.user_properties[i].key);
        pack_string(stat, stat->pubcomp.user_properties[i].value);
    }
}

static void pack_unsubscribe_props(struct mqtt_client* stat)
{
    pack_variable_size(stat, estimate_unsubscribe_prop_size(stat));
    for (int i = 0; i < stat->unsubscribe.user_properties_count; i++) {
        pack_byte(stat, MQTT_USER_PROPERTY_ID);
        pack_string(stat, stat->unsubscribe.user_properties[i].key);
        pack_string(stat, stat->unsubscribe.user_properties[i].value);
    }
}

/***** Validity checks ***************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static int validate_subscribe_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check subscription entries
    if (stat->subscribe.entries && stat->subscribe.entry_count > 0) {
        for (unsigned int i = 0; i < stat->subscribe.entry_count; i++) {
            if (stat->subscribe.entries[i].topic &&
                    !is_valid_utf8(stat->subscribe.entries[i].topic, strlen(stat->subscribe.entries[i].topic))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    // Check user properties if present
    if (stat->subscribe.user_properties && stat->subscribe.user_properties_count > 0) {
        for (int i = 0; i < stat->subscribe.user_properties_count; i++) {
            if (stat->subscribe.user_properties[i].key &&
                    !is_valid_utf8(stat->subscribe.user_properties[i].key, strlen(stat->subscribe.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            if (stat->subscribe.user_properties[i].value &&
                    !is_valid_utf8(stat->subscribe.user_properties[i].value, strlen(stat->subscribe.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_connect_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check client_id (required field)
    if (stat->connect.client_id && !is_valid_utf8(stat->connect.client_id, strlen(stat->connect.client_id))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check username if present
    if (stat->connect.user && !is_valid_utf8(stat->connect.user, strlen(stat->connect.user))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check password if present
    if (stat->connect.passwd && !is_valid_utf8(stat->connect.passwd, strlen(stat->connect.passwd))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check will topic if present
    if (stat->connect.will_topic && !is_valid_utf8(stat->connect.will_topic, strlen(stat->connect.will_topic))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check will data if present
    if (stat->connect.will_data && !is_valid_utf8(stat->connect.will_data, strlen(stat->connect.will_data))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check will properties if present
    if (stat->connect.will.topic && !is_valid_utf8(stat->connect.will.topic, strlen(stat->connect.will.topic))) {
        return ERROR_INVALID_ENCODING;
    }

    if (stat->connect.will.content_type
            && !is_valid_utf8(stat->connect.will.content_type, strlen(stat->connect.will.content_type))) {
        return ERROR_INVALID_ENCODING;
    }

    if (stat->connect.will.response_topic
            && !is_valid_utf8(stat->connect.will.response_topic, strlen(stat->connect.will.response_topic))) {
        return ERROR_INVALID_ENCODING;
    }

    return OK;
}

static int validate_disconnect_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check reason_string if present
    if (stat->disconn.reason_string && !is_valid_utf8(stat->disconn.reason_string, strlen(stat->disconn.reason_string))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check server_reference if present
    if (stat->disconn.server_reference
            && !is_valid_utf8(stat->disconn.server_reference, strlen(stat->disconn.server_reference))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user_properties if present
    if (stat->disconn.user_properties && stat->disconn.user_properties_count > 0) {
        for (int i = 0; i < stat->disconn.user_properties_count; i++) {
            // Check property name
            if (stat->disconn.user_properties[i].key &&
                    !is_valid_utf8(stat->disconn.user_properties[i].key, strlen(stat->disconn.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->disconn.user_properties[i].value &&
                    !is_valid_utf8(stat->disconn.user_properties[i].value, strlen(stat->disconn.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_publish_utf8_strings(const struct mqtt_client *stat, const struct mqtt_pub_packet *msg)
{
    if (!stat || !msg) {
        return ERROR_NULL_REFERENCE;
    }

    // Check topic (required field)
    if (!msg->topic || !is_valid_utf8(msg->topic, strlen(msg->topic))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check publish properties if present
    if (stat->publish.content_type && !is_valid_utf8(stat->publish.content_type, strlen(stat->publish.content_type))) {
        return ERROR_INVALID_ENCODING;
    }

    if (stat->publish.response_topic
            && !is_valid_utf8(stat->publish.response_topic, strlen(stat->publish.response_topic))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user properties if present
    if (stat->publish.user_properties && stat->publish.user_properties_count > 0) {
        for (int i = 0; i < stat->publish.user_properties_count; i++) {
            // Check property name
            if (stat->publish.user_properties[i].key &&
                    !is_valid_utf8(stat->publish.user_properties[i].key, strlen(stat->publish.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->publish.user_properties[i].value &&
                    !is_valid_utf8(stat->publish.user_properties[i].value, strlen(stat->publish.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_puback_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check reason_string if present
    if (stat->puback.reason_string && !is_valid_utf8(stat->puback.reason_string, strlen(stat->puback.reason_string))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user_properties if present
    if (stat->puback.user_properties && stat->puback.user_properties_count > 0) {
        for (int i = 0; i < stat->puback.user_properties_count; i++) {
            // Check property name
            if (stat->puback.user_properties[i].key &&
                    !is_valid_utf8(stat->puback.user_properties[i].key, strlen(stat->puback.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->puback.user_properties[i].value &&
                    !is_valid_utf8(stat->puback.user_properties[i].value, strlen(stat->puback.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_pubrec_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check reason_string if present
    if (stat->pubrec.reason_string && !is_valid_utf8(stat->pubrec.reason_string, strlen(stat->pubrec.reason_string))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user_properties if present
    if (stat->pubrec.user_properties && stat->pubrec.user_properties_count > 0) {
        for (int i = 0; i < stat->pubrec.user_properties_count; i++) {
            // Check property name
            if (stat->pubrec.user_properties[i].key &&
                    !is_valid_utf8(stat->pubrec.user_properties[i].key, strlen(stat->pubrec.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->pubrec.user_properties[i].value &&
                    !is_valid_utf8(stat->pubrec.user_properties[i].value, strlen(stat->pubrec.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_pubrel_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check reason_string if present
    if (stat->pubrel.reason_string && !is_valid_utf8(stat->pubrel.reason_string, strlen(stat->pubrel.reason_string))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user_properties if present
    if (stat->pubrel.user_properties && stat->pubrel.user_properties_count > 0) {
        for (int i = 0; i < stat->pubrel.user_properties_count; i++) {
            // Check property name
            if (stat->pubrel.user_properties[i].key &&
                    !is_valid_utf8(stat->pubrel.user_properties[i].key, strlen(stat->pubrel.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->pubrel.user_properties[i].value &&
                    !is_valid_utf8(stat->pubrel.user_properties[i].value, strlen(stat->pubrel.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_pubcomp_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check reason_string if present
    if (stat->pubcomp.reason_string && !is_valid_utf8(stat->pubcomp.reason_string, strlen(stat->pubcomp.reason_string))) {
        return ERROR_INVALID_ENCODING;
    }

    // Check user_properties if present
    if (stat->pubcomp.user_properties && stat->pubcomp.user_properties_count > 0) {
        for (int i = 0; i < stat->pubcomp.user_properties_count; i++) {
            // Check property name
            if (stat->pubcomp.user_properties[i].key &&
                    !is_valid_utf8(stat->pubcomp.user_properties[i].key, strlen(stat->pubcomp.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            // Check property value
            if (stat->pubcomp.user_properties[i].value &&
                    !is_valid_utf8(stat->pubcomp.user_properties[i].value, strlen(stat->pubcomp.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

static int validate_unsubscribe_utf8_strings(const struct mqtt_client *stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    // Check unsubscribe entries
    if (stat->unsubscribe.entries && stat->unsubscribe.entry_count > 0) {
        for (unsigned int i = 0; i < stat->unsubscribe.entry_count; i++) {
            if (stat->unsubscribe.entries[i].topic &&
                    !is_valid_utf8(stat->unsubscribe.entries[i].topic, strlen(stat->unsubscribe.entries[i].topic))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    // Check user properties if present
    if (stat->unsubscribe.user_properties && stat->unsubscribe.user_properties_count > 0) {
        for (int i = 0; i < stat->unsubscribe.user_properties_count; i++) {
            if (stat->unsubscribe.user_properties[i].key &&
                    !is_valid_utf8(stat->unsubscribe.user_properties[i].key, strlen(stat->unsubscribe.user_properties[i].key))) {
                return ERROR_INVALID_ENCODING;
            }

            if (stat->unsubscribe.user_properties[i].value &&
                    !is_valid_utf8(stat->unsubscribe.user_properties[i].value, strlen(stat->unsubscribe.user_properties[i].value))) {
                return ERROR_INVALID_ENCODING;
            }
        }
    }

    return OK;
}

/***** Packet generation *************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static void make_subscribe(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_subscribe_prop_size(stat);
    uint32_t rsize = 2 + get_variable_size_byte_count(prop_size) + prop_size; // packet_id + props

    // Calculate size for all subscription entries
    for (unsigned int i = 0; i < stat->subscribe.entry_count; i++) {
        rsize += STRLEN(stat->subscribe.entries[i].topic) + 1; // topic + subscription options
    }

    if (stat->pout) {
        write_fixed_header(stat, SUBSCRIBE, 0x02, rsize); // Fixed header flags = 0010 (reserved)
        pack_word(stat, stat->subscribe.packet_id);
        pack_subscribe_props(stat);

        for (unsigned int i = 0; i < stat->subscribe.entry_count; i++) {
            pack_string(stat, stat->subscribe.entries[i].topic);

            // Pack subscription options byte
            uint8_t options = 0;
            options |= (stat->subscribe.entries[i].qos & 0x03);
            if (stat->subscribe.entries[i].no_local) {
                options |= 0x04;
            }
            if (stat->subscribe.entries[i].retain_as_published) {
                options |= 0x08;
            }
            options |= (stat->subscribe.entries[i].retain_handling & 0x03) << 4;

            pack_byte(stat, options);
        }
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_connect(struct mqtt_client* stat)
{
    uint8_t flags = (stat->connect.will_qos & 0x03) << 2;
    if (stat->connect.clean_start) {
        SET(flags, BIT(1));
    }
    if (stat->connect.will_flag) {
        SET(flags, BIT(2));
    }
    if (stat->connect.pw_flag) {
        SET(flags, BIT(6));
    }
    if (stat->connect.un_flag) {
        SET(flags, BIT(7));
    }
    uint8_t plen = estimate_conn_prop_size(stat);
    uint32_t rsize = 10 + plen + STRLEN(stat->connect.client_id) + get_variable_size_byte_count(plen);
    if (stat->connect.will_flag) {
        plen = estimate_will_prop_size(stat);
        rsize += plen + STRLEN(stat->connect.will.topic) + BLEN(stat->connect.will.payload) + get_variable_size_byte_count(
                     plen);
    }
    if (stat->connect.un_flag) {
        rsize += STRLEN(stat->connect.user);
    }
    if (stat->connect.pw_flag) {
        rsize += STRLEN(stat->connect.passwd);
    }
    if (stat->pout) {
        write_fixed_header(stat, CONNECT, 0, rsize);
        pack_string(stat, "MQTT");
        pack_byte(stat, MQTT_PROTOCOL_VERSION);
        pack_byte(stat, flags);
        pack_word(stat, stat->connect.keep_alive);
        pack_conn_props(stat);
        pack_string(stat, stat->connect.client_id);
        if (stat->connect.will_flag) {
            pack_will_props(stat);
            pack_string(stat, stat->connect.will.topic);
            pack_binary(stat, &stat->connect.will.payload);
            if (stat->connect.un_flag) {
                pack_string(stat, stat->connect.user);
            }
            if (stat->connect.pw_flag) {
                pack_string(stat, stat->connect.passwd);
            }
        }
    }
    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_disconnect(struct mqtt_client *stat)
{
    uint32_t rsize = 1;
    bool disconnect_with_properties = stat->disconn.reason_string ||
                                      stat->disconn.server_reference ||
                                      stat->disconn.session_expiry_interval ||
                                      stat->disconn.user_properties_count;
    if (disconnect_with_properties) {
        rsize += estimate_disconn_prop_size(stat);
        rsize += get_variable_size_byte_count(rsize);
    }
    if (stat->pout) {
        write_fixed_header(stat, DISCONNECT, 0, rsize);
        pack_byte(stat, stat->disconn.reason_code);
        if (disconnect_with_properties) {
            pack_disconn_props(stat);
        }
    }
    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_publish(struct mqtt_client* stat, struct mqtt_pub_packet* msg)
{
    uint8_t flags = 0;

    // Set flags based on message properties
    if (msg->dup) {
        SET(flags, BIT(3));
    }
    if (msg->retain) {
        SET(flags, BIT(0));
    }
    flags |= (msg->qos & 0x03) << 1;

    // Calculate remaining length
    uint32_t prop_size = estimate_publish_prop_size(stat);
    uint32_t rsize = STRLEN(msg->topic) + get_variable_size_byte_count(prop_size) + prop_size;

    // Add packet identifier for QoS > 0
    if (msg->qos > 0) {
        rsize += 2;  // 2 bytes for packet identifier
    }

    // Add payload length
    rsize += msg->payload.len;

    if (stat->pout) {
        write_fixed_header(stat, PUBLISH, flags, rsize);

        // Pack topic name
        pack_string(stat, msg->topic);

        // Pack packet identifier for QoS > 0
        if (msg->qos > 0) {
            pack_word(stat, msg->packet_id);
        }

        // Pack properties
        pack_publish_props(stat);

        // Pack payload
        if (msg->payload.data && msg->payload.len > 0) {
            for (uint32_t i = 0; i < msg->payload.len; i++) {
                pack_byte(stat, msg->payload.data[i]);
            }
        }
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_pingreq(struct mqtt_client* stat)
{
    uint32_t rsize = 0; // PINGREQ has no variable header or payload

    if (stat->pout) {
        write_fixed_header(stat, PINGREQ, 0, rsize);
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_puback(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_puback_prop_size(stat);
    uint32_t rsize = 2 + 1; // packet_id + reason_code

    // Add properties size if there are properties
    if (prop_size > 0) {
        rsize += get_variable_size_byte_count(prop_size) + prop_size;
    } else {
        rsize += 1; // properties length = 0
    }

    if (stat->pout) {
        write_fixed_header(stat, PUBACK, 0, rsize);
        pack_word(stat, stat->puback.packet_id);
        pack_byte(stat, stat->puback.reason_code);
        pack_puback_props(stat);
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_pubrec(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_pubrec_prop_size(stat);
    uint32_t rsize = 2 + 1; // packet_id + reason_code

    // Add properties size if there are properties
    if (prop_size > 0) {
        rsize += get_variable_size_byte_count(prop_size) + prop_size;
    } else {
        rsize += 1; // properties length = 0
    }

    if (stat->pout) {
        write_fixed_header(stat, PUBREC, 0, rsize);
        pack_word(stat, stat->pubrec.packet_id);
        pack_byte(stat, stat->pubrec.reason_code);
        pack_pubrec_props(stat);
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_pubrel(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_pubrel_prop_size(stat);
    uint32_t rsize = 2 + 1; // packet_id + reason_code

    // Add properties size if there are properties
    if (prop_size > 0) {
        rsize += get_variable_size_byte_count(prop_size) + prop_size;
    } else {
        rsize += 1; // properties length = 0
    }

    if (stat->pout) {
        write_fixed_header(stat, PUBREL, 0x02, rsize); // Fixed header flags = 0010 (reserved)
        pack_word(stat, stat->pubrel.packet_id);
        pack_byte(stat, stat->pubrel.reason_code);
        pack_pubrel_props(stat);
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_pubcomp(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_pubcomp_prop_size(stat);
    uint32_t rsize = 2 + 1; // packet_id + reason_code

    // Add properties size if there are properties
    if (prop_size > 0) {
        rsize += get_variable_size_byte_count(prop_size) + prop_size;
    } else {
        rsize += 1; // properties length = 0
    }

    if (stat->pout) {
        write_fixed_header(stat, PUBCOMP, 0, rsize);
        pack_word(stat, stat->pubcomp.packet_id);
        pack_byte(stat, stat->pubcomp.reason_code);
        pack_pubcomp_props(stat);
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

static void make_unsubscribe(struct mqtt_client* stat)
{
    uint32_t prop_size = estimate_unsubscribe_prop_size(stat);
    uint32_t rsize = 2 + get_variable_size_byte_count(prop_size) + prop_size; // packet_id + props

    // Calculate size for all unsubscribe entries
    for (unsigned int i = 0; i < stat->unsubscribe.entry_count; i++) {
        rsize += STRLEN(stat->unsubscribe.entries[i].topic);
    }

    if (stat->pout) {
        write_fixed_header(stat, UNSUBSCRIBE, 0x02, rsize); // Fixed header flags = 0010 (reserved)
        pack_word(stat, stat->unsubscribe.packet_id);
        pack_unsubscribe_props(stat);

        for (unsigned int i = 0; i < stat->unsubscribe.entry_count; i++) {
            pack_string(stat, stat->unsubscribe.entries[i].topic);
        }
    }

    stat->packet_size = rsize + estimate_fixed_header_size(rsize);
}

/***** Packet ID management **********************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static int reserve_packet_slot_for_answer(struct mqtt_client *stat, mqtt_packet_type await)
{
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (!stat->pending[i].packet_id) {
            stat->pending[i].packet_id = ++stat->packet_id_count;
            if (!stat->pending[i].packet_id) {
                stat->pending[i].packet_id = 1; // in case of overflow
            }
            stat->pending[i].await_packet_type = await;
            return stat->pending[i].packet_id;
        }
    }
    return ERROR_OUT_OF_RESOURCE;
}

static int reserve_packet_slot_for_request(struct mqtt_client *stat, uint16_t packet_id, mqtt_packet_type request)
{
    if (!packet_id) {
        return ERROR_INVALID_PACKET_ID;
    }
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (!stat->pending[i].packet_id) {
            stat->pending[i].packet_id = packet_id;
            stat->pending[i].await_packet_type = request;
            return OK;
        }
    }
    return ERROR_OUT_OF_RESOURCE;
}

static int free_packet_slot(struct mqtt_client *stat, uint16_t packet_id)
{
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (stat->pending[i].packet_id == packet_id) {
            stat->pending[i].packet_id = 0;
            stat->pending[i].await_packet_type = UNKNOWN;
            return OK;
        }
    }
    return ERROR_INVALID_PACKET_ID;
}

static mqtt_packet_type get_expected_packet_answer(struct mqtt_client *stat, uint16_t packet_id)
{
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (stat->pending[i].packet_id == packet_id) {
            return stat->pending[i].await_packet_type;
        }
    }
    return UNKNOWN;
}

static bool await_for_packet(struct mqtt_client *stat, mqtt_packet_type type)
{
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (stat->pending[i].await_packet_type == type) {
            return true;
        }
    }
    return false;
}

/***** Packet processing *************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

static inline void connack_default_properties(struct mqtt_client *stat)
{
    stat->connack.max_qos = 2;
    stat->connack.retain_avail = true;
    stat->connack.wildcard_sub_avail = true;
    stat->connack.sub_id_avail = true;
    stat->connack.shared_sub_avail = true;
    stat->connack.server_keep_alive = stat->connect.keep_alive;
    stat->connack.max_packet_size = stat->connect.max_packet_size;
}

static int process_connack_poperties(struct mqtt_client *stat, int len)
{
    char *key = NULL;
    char *value = NULL;
    while (len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        switch (prop_id) {
        case MQTT_ACK_SERVER_REFERENCE_ID:
            stat->connack.server_reference = unpack_string(stat);
            len -= (int) STRLEN(stat->connack.server_reference);
            break;

        case MQTT_CON_RESPONSE_INFO_ID:
            stat->connack.response_info = unpack_string(stat);
            len -= (int) STRLEN(stat->connack.response_info);
            break;

        case MQTT_CON_REQUEST_RESPONSE_INFO_ID:
        case MQTT_CON_REQUEST_PROBLEM_INF_ID:
            unpack_byte(stat); // Single byte
            len -= 1;
            break;

        case MQTT_CON_TOPIC_ALIAS_MAXIMUM_ID:
            stat->connack.topic_alias_max = unpack_word(stat);
            len -= 2;
            break;

        case MQTT_CON_RECEIVE_MAXIMUM_ID:
            stat->connack.recv_max = unpack_word(stat);
            len -= 2;
            break;

        case MQTT_CON_MAXIMUM_QOS_ID:
            stat->connack.max_qos = unpack_byte(stat);
            len -= 1;
            break;

        case MQTT_CON_RETAIN_AVAILABLE_ID:
            stat->connack.retain_avail = unpack_byte(stat) & 0x01;
            len -= 1;
            break;

        case MQTT_CON_MAXIMUM_PACKET_SIZE_ID:
            stat->connack.max_packet_size = unpack_dword(stat);
            len -= 4;
            break;

        case MQTT_ACK_ASSIGNED_CLIENT_ID:
            stat->connack.assigned_client_id = unpack_string(stat);
            len -= (int)STRLEN(stat->connack.assigned_client_id);
            break;

        case MQTT_REASON_STRING_ID:
            stat->connack.reason_string = unpack_string(stat);
            len -= (int)STRLEN(stat->connack.reason_string);
            break;

        case MQTT_USER_PROPERTY_ID:
            key = unpack_string(stat);
            value = unpack_string(stat);
            len -= (int)(STRLEN(key) + STRLEN(value));
            mqtt_user_property(stat, CONNACK, key, value);
            free(key);
            free(value);
            break;

        case MQTT_ACK_WILDCARD_SUB_AVAIL_ID:
            stat->connack.wildcard_sub_avail = unpack_byte(stat) & 0x01;
            len -= 1;
            break;

        case MQTT_ACK_SUB_ID_AVAIL_ID:
            stat->connack.sub_id_avail = unpack_byte(stat) & 0x01;
            len -= 1;
            break;

        case MQTT_ACK_SHARED_SUB_AVAIL_ID:
            stat->connack.shared_sub_avail = unpack_byte(stat) & 0x01;
            len -= 1;
            break;

        case MQTT_ACK_SEVER_KEEP_ALIVE_ID:
            stat->connack.server_keep_alive = unpack_word(stat);
            len -= 2;
            break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
        --len;
    }
    if (len < 0) {
        return ERROR_MALFORMED_PACKET;
    }
    return OK;
}

static int process_connack(struct mqtt_client *stat)
{
    int result = OK;
    stat->connack.ack_flag = unpack_byte(stat) & 0x01;
    stat->connack.reason = unpack_byte(stat);
    if (stat->connack.reason & 0x80) {
        return ERROR_SERVER_DECLINED;
    }
    uint32_t prop_len = unpack_variable_size(stat);
    connack_default_properties(stat);
    if (prop_len > 0) {
        result = process_connack_poperties(stat, (int)prop_len);
    }
    if (SUCCESSFUL(result)) {
        stat->connected = true;
        stat->expected_ptypes |= BIT(DISCONNECT) | BIT(PUBLISH);
        mqtt_connected(stat);
    }
    return result;
}

static int process_publish_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_PUB_PAYLOAD_FORMAT_INDICATOR_ID:
            stat->received_publish.payload_format_indicator = unpack_byte(stat);
            prop_len--;
            break;

        case MQTT_PUB_MESSAGE_EXPIRY_INTERVAL_ID:
            stat->received_publish.message_expiry_interval = unpack_dword(stat);
            prop_len -= 4;
            break;

        case MQTT_PUB_TOPIC_ALIAS_ID:
            stat->received_publish.topic_alias = unpack_word(stat);
            prop_len -= 2;
            break;

        case MQTT_PUB_RESPONSE_TOPIC_ID:
            stat->received_publish.response_topic = unpack_string(stat);
            if (stat->received_publish.response_topic) {
                prop_len -= strlen(stat->received_publish.response_topic) + 2;
            } else {
                prop_len -= 2;
            }
            break;

        case MQTT_PUB_CORRELATION_DATA_ID: {
            uint16_t data_len = unpack_word(stat);
            if (data_len > 0 && data_len <= sizeof(stat->received_publish.correlation_data_buffer)) {
                stat->received_publish.correlation_data.data = stat->received_publish.correlation_data_buffer;
                stat->received_publish.correlation_data.len = data_len;
                stat->received_publish.correlation_data.maxlen = sizeof(stat->received_publish.correlation_data_buffer);

                for (uint16_t i = 0; i < data_len; i++) {
                    stat->received_publish.correlation_data.data[i] = unpack_byte(stat);
                }
            } else {
                // Skip data if too large
                for (uint16_t i = 0; i < data_len; i++) {
                    unpack_byte(stat);
                }
            }
            prop_len -= data_len + 2;
        }
        break;

        case MQTT_PUB_CONTENT_TYPE_ID:
            stat->received_publish.content_type = unpack_string(stat);
            if (stat->received_publish.content_type) {
                prop_len -= strlen(stat->received_publish.content_type) + 2;
            } else {
                prop_len -= 2;
            }
            break;

        case MQTT_PUB_SUBSCRIPTION_IDENTIFIER_ID:
            stat->received_publish.subscription_identifier = unpack_variable_size(stat);
            prop_len -= get_variable_size_byte_count(stat->received_publish.subscription_identifier);
            break;

        case MQTT_PUB_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, PUBLISH, key, value);
            } else {
                prop_len -= 4; // Minimum for two empty strings
            }

            free(key);
            free(value);
        }
        break;

        default:
            // Unknown property - skip remaining properties to avoid corruption
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_publish(struct mqtt_client *stat, uint8_t fixed_header_flags)
{
    int result = OK;

    // Free allocated strings from last received packet
    if (stat->received_publish.topic) {
        free((void *)stat->received_publish.topic);
        stat->received_publish.topic = NULL;
    }
    if (stat->received_publish.response_topic) {
        free((void *)stat->received_publish.response_topic);
        stat->received_publish.response_topic = NULL;
    }
    if (stat->received_publish.content_type) {
        free((void *)stat->received_publish.content_type);
        stat->received_publish.content_type = NULL;
    }

    // Clear previous publish data
    memset(&stat->received_publish, 0, sizeof(stat->received_publish));

    // Unpack topic name
    char* topic = unpack_string(stat);
    if (!topic) {
        return ERROR_MALFORMED_PACKET;
    }

    // Validate UTF-8 encoding of topic
    if (!is_valid_utf8(topic, strlen(topic))) {
        free(topic);
        return ERROR_INVALID_ENCODING;
    }

    stat->received_publish.topic = topic;

    // Extract QoS from fixed header flags (bits 1-2)
    uint8_t qos = (fixed_header_flags >> 1) & 0x03;
    stat->received_publish.qos = qos;

    // Extract other flags from fixed header
    stat->received_publish.dup = (fixed_header_flags & 0x08) != 0;
    stat->received_publish.retain = (fixed_header_flags & 0x01) != 0;

    // Unpack packet identifier for QoS > 0
    if (qos > 0) {
        stat->received_publish.packet_id = unpack_word(stat);
    }

    // Unpack properties length
    uint32_t prop_len = unpack_variable_size(stat);

    // Process properties
    if (prop_len > 0) {
        result = process_publish_properties(stat, prop_len);
        if (FAILED(result)) {
            goto cleanup;
        }
    }

    // Calculate payload length (remaining bytes after properties)
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        stat->received_publish.payload.len = stat->inp.len - bytes_consumed;
        stat->received_publish.payload.data = stat->pin;
    }

    // Validate payload format if indicator is set
    if (stat->received_publish.payload_format_indicator == 1) {
        // Payload should be UTF-8 encoded
        if (stat->received_publish.payload.data && stat->received_publish.payload.len > 0) {
            if (!is_valid_utf8((char*)stat->received_publish.payload.data, stat->received_publish.payload.len)) {
                result = ERROR_INVALID_ENCODING;
                goto cleanup;
            }
        }
    }

    // Handle QoS acknowledgments
    switch (qos) {
    case 1:
        // QoS 1 - send PUBACK
        mqtt_puback(stat, stat->received_publish.packet_id);
        break;

    case 2:
        // QoS 2 - send PUBREC
        mqtt_pubrec(stat, stat->received_publish.packet_id);
        break;

    default:
        // QoS 0 - no acknowledgment needed
        break;
    }

    // Set flag indicating new message is available
    stat->message_available = true;
    mqtt_received_publish(stat);

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->received_publish.topic) {
            free((void*)stat->received_publish.topic);
            stat->received_publish.topic = NULL;
        }
        if (stat->received_publish.response_topic) {
            free((void*)stat->received_publish.response_topic);
            stat->received_publish.response_topic = NULL;
        }
        if (stat->received_publish.content_type) {
            free((void*)stat->received_publish.content_type);
            stat->received_publish.content_type = NULL;
        }
    }

    return result;
}

static int process_suback_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_SUBACK_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                prop_len -= strlen(reason_string) + 2;
                mqtt_user_property(stat, SUBACK, "reason_string", reason_string);
                free(reason_string);
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_SUBACK_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, SUBACK, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_suback(struct mqtt_client *stat)
{
    int result = OK;
    uint16_t packet_id = unpack_word(stat);

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, packet_id);
    if (expected != SUBACK) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Process properties
    uint32_t prop_len = unpack_variable_size(stat);
    if (prop_len > 0) {
        result = process_suback_properties(stat, prop_len);
        if (FAILED(result)) {
            return result;
        }
    }

    // Process reason codes for each subscription
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    uint32_t remaining_bytes = stat->inp.len - bytes_consumed;

    int sub_num = 0;
    for (uint32_t i = 0; i < remaining_bytes; i++) {
        uint8_t reason_code = unpack_byte(stat);

        // Check if subscription was successful
        if (reason_code <= MQTT_REASON_GRANTED_QOS_2) {
            // Subscription granted with QoS 0, 1, or 2
            mqtt_subscription_granted(stat, packet_id, sub_num++);
            continue;
        } else if (reason_code >= 0x80) {
            // Subscription failed - reason codes 0x80 and above indicate failure
            // Common failure codes: 0x80 (Unspecified error), 0x83 (Implementation specific error),
            // 0x87 (Not authorized), 0x8F (Topic filter invalid), 0x91 (Packet identifier in use),
            // 0x97 (Quota exceeded), 0xA1 (Subscription identifiers not supported),
            // 0xA2 (Wildcard subscriptions not supported)

            // For now, we continue processing other subscriptions
            // User code can check individual reason codes through a callback if needed
            mqtt_subscription_declined(stat, packet_id, sub_num++, reason_code);
        }
    }

    // Free the packet slot
    free_packet_slot(stat, packet_id);

    // Remove SUBACK from expected packet types
    if (!await_for_packet(stat, SUBACK)) {
        stat->expected_ptypes &= ~BIT(SUBACK);
    }

    return result;
}

static int process_disconnect_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_DISC_SESSION_EXPIRY_INTERVAL_ID:
            stat->disconn.session_expiry_interval = unpack_dword(stat);
            prop_len -= 4;
            break;

        case MQTT_DISC_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->disconn.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_DISC_SERVER_REFERENCE_ID: {
            char* server_reference = unpack_string(stat);
            if (server_reference) {
                stat->disconn.server_reference = server_reference;
                prop_len -= strlen(server_reference) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_DISC_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, DISCONNECT, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_disconnect(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated strings
    if (stat->disconn.reason_string) {
        free((void*)stat->disconn.reason_string);
        stat->disconn.reason_string = NULL;
    }
    if (stat->disconn.server_reference) {
        free((void*)stat->disconn.server_reference);
        stat->disconn.server_reference = NULL;
    }

    // Clear disconnect structure
    memset(&stat->disconn, 0, sizeof(stat->disconn));

    // Unpack reason code
    stat->disconn.reason_code = unpack_byte(stat);

    // Check if there are properties to process
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        // Process properties
        uint32_t prop_len = unpack_variable_size(stat);

        if (prop_len > 0) {
            result = process_disconnect_properties(stat, prop_len);
            if (FAILED(result)) {
                goto cleanup;
            }
        }
    }

    // Update connection state
    stat->connected = false;
    stat->expected_ptypes = BIT(PINGREQ);

    // Call user callback
    mqtt_received_disconnect(stat, (mqtt_reason_code)stat->disconn.reason_code);

    // Close network connection
    if (stat->net.close_conn) {
        stat->net.close_conn(stat);
    }

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->disconn.reason_string) {
            free((void*)stat->disconn.reason_string);
            stat->disconn.reason_string = NULL;
        }
        if (stat->disconn.server_reference) {
            free((void*)stat->disconn.server_reference);
            stat->disconn.server_reference = NULL;
        }
    }

    return result;
}

static int process_puback_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_PUBACK_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->puback.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, PUBACK, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_puback(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated reason string
    if (stat->puback.reason_string) {
        free((void*)stat->puback.reason_string);
        stat->puback.reason_string = NULL;
    }

    // Clear PUBACK structure
    memset(&stat->puback, 0, sizeof(stat->puback));

    // Unpack packet identifier
    stat->puback.packet_id = unpack_word(stat);

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, stat->puback.packet_id);
    if (expected != PUBACK) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Check if there's a reason code and properties
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        // Unpack reason code
        stat->puback.reason_code = unpack_byte(stat);

        // Check if there are properties
        bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
        if (bytes_consumed < stat->inp.len) {
            // Process properties
            uint32_t prop_len = unpack_variable_size(stat);

            if (prop_len > 0) {
                result = process_puback_properties(stat, prop_len);
                if (FAILED(result)) {
                    goto cleanup;
                }
            }
        }
    } else {
        // No reason code or properties - assume success
        stat->puback.reason_code = 0;
    }

    // Free the packet slot
    free_packet_slot(stat, stat->puback.packet_id);

    // Remove PUBACK from expected packet types if no more pending
    if (!await_for_packet(stat, PUBACK)) {
        stat->expected_ptypes &= ~BIT(PUBACK);
    }

    // Call user callback
    mqtt_publish_acknowledged(stat, stat->puback.packet_id, stat->puback.reason_code);

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->puback.reason_string) {
            free((void*)stat->puback.reason_string);
            stat->puback.reason_string = NULL;
        }
    }

    return result;
}

static int process_pubrec_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_PUBREC_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->pubrec.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, PUBREC, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_pubrec(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated reason string
    if (stat->pubrec.reason_string) {
        free((void*)stat->pubrec.reason_string);
        stat->pubrec.reason_string = NULL;
    }

    // Clear PUBREC structure
    memset(&stat->pubrec, 0, sizeof(stat->pubrec));

    // Unpack packet identifier
    stat->pubrec.packet_id = unpack_word(stat);

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, stat->pubrec.packet_id);
    if (expected != PUBREC) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Check if there's a reason code and properties
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        // Unpack reason code
        stat->pubrec.reason_code = unpack_byte(stat);

        // Check if there are properties
        bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
        if (bytes_consumed < stat->inp.len) {
            // Process properties
            uint32_t prop_len = unpack_variable_size(stat);

            if (prop_len > 0) {
                result = process_pubrec_properties(stat, prop_len);
                if (FAILED(result)) {
                    goto cleanup;
                }
            }
        }
    } else {
        // No reason code or properties - assume success
        stat->pubrec.reason_code = 0;
    }

    // Update packet slot to expect PUBCOMP instead of PUBREC
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (stat->pending[i].packet_id == stat->pubrec.packet_id) {
            stat->pending[i].await_packet_type = PUBCOMP;
            break;
        }
    }

    // Remove PUBREC from expected packet types if no more pending
    if (!await_for_packet(stat, PUBREC)) {
        stat->expected_ptypes &= ~BIT(PUBREC);
    }

    // Add PUBCOMP to expected packet types
    stat->expected_ptypes |= BIT(PUBCOMP);

    // Send PUBREL in response to PUBREC
    result = mqtt_pubrel(stat, stat->pubrec.packet_id);

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->pubrec.reason_string) {
            free((void*)stat->pubrec.reason_string);
            stat->pubrec.reason_string = NULL;
        }
    }

    return result;
}

static int process_pubrel_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_PUBREL_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->pubrel.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, PUBREL, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_pubrel(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated reason string
    if (stat->pubrel.reason_string) {
        free((void*)stat->pubrel.reason_string);
        stat->pubrel.reason_string = NULL;
    }

    // Clear PUBREL structure
    memset(&stat->pubrel, 0, sizeof(stat->pubrel));

    // Unpack packet identifier
    stat->pubrel.packet_id = unpack_word(stat);

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, stat->pubrel.packet_id);
    if (expected != PUBREL) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Check if there's a reason code and properties
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        // Unpack reason code
        stat->pubrel.reason_code = unpack_byte(stat);

        // Check if there are properties
        bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
        if (bytes_consumed < stat->inp.len) {
            // Process properties
            uint32_t prop_len = unpack_variable_size(stat);

            if (prop_len > 0) {
                result = process_pubrel_properties(stat, prop_len);
                if (FAILED(result)) {
                    goto cleanup;
                }
            }
        }
    } else {
        // No reason code or properties - assume success
        stat->pubrel.reason_code = 0;
    }

    // Free the packet slot
    free_packet_slot(stat, stat->pubrel.packet_id);

    // Remove PUBREL from expected packet types if no more pending
    if (!await_for_packet(stat, PUBREL)) {
        stat->expected_ptypes &= ~BIT(PUBREL);
    }

    // Send PUBCOMP in response to PUBREL
    result = mqtt_pubcomp(stat, stat->pubrel.packet_id);

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->pubrel.reason_string) {
            free((void*)stat->pubrel.reason_string);
            stat->pubrel.reason_string = NULL;
        }
    }

    return result;
}

static int process_pubcomp_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_PUBCOMP_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->pubcomp.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, PUBCOMP, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_pubcomp(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated reason string
    if (stat->pubcomp.reason_string) {
        free((void*)stat->pubcomp.reason_string);
        stat->pubcomp.reason_string = NULL;
    }

    // Clear PUBCOMP structure
    memset(&stat->pubcomp, 0, sizeof(stat->pubcomp));

    // Unpack packet identifier
    stat->pubcomp.packet_id = unpack_word(stat);

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, stat->pubcomp.packet_id);
    if (expected != PUBCOMP) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Check if there's a reason code and properties
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    if (bytes_consumed < stat->inp.len) {
        // Unpack reason code
        stat->pubcomp.reason_code = unpack_byte(stat);

        // Check if there are properties
        bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
        if (bytes_consumed < stat->inp.len) {
            // Process properties
            uint32_t prop_len = unpack_variable_size(stat);

            if (prop_len > 0) {
                result = process_pubcomp_properties(stat, prop_len);
                if (FAILED(result)) {
                    goto cleanup;
                }
            }
        }
    } else {
        // No reason code or properties - assume success
        stat->pubcomp.reason_code = 0;
    }

    // Free the packet slot - QoS 2 flow is now complete
    free_packet_slot(stat, stat->pubcomp.packet_id);

    // Remove PUBCOMP from expected packet types if no more pending
    if (!await_for_packet(stat, PUBCOMP)) {
        stat->expected_ptypes &= ~BIT(PUBCOMP);
    }

    // Call user callback to notify QoS 2 publish is complete
    mqtt_publish_completed(stat, stat->pubcomp.packet_id, stat->pubcomp.reason_code);

cleanup:
    if (FAILED(result)) {
        // Free allocated strings on error
        if (stat->pubcomp.reason_string) {
            free((void*)stat->pubcomp.reason_string);
            stat->pubcomp.reason_string = NULL;
        }
    }

    return result;
}

static int process_unsuback_properties(struct mqtt_client *stat, uint32_t prop_len)
{
    while (prop_len > 0) {
        uint8_t prop_id = unpack_byte(stat);
        prop_len--;

        switch (prop_id) {
        case MQTT_UNSUBACK_REASON_STRING_ID: {
            char* reason_string = unpack_string(stat);
            if (reason_string) {
                stat->unsuback.reason_string = reason_string;
                prop_len -= strlen(reason_string) + 2;
            } else {
                prop_len -= 2;
            }
        }
        break;

        case MQTT_UNSUBACK_USER_PROPERTY_ID: {
            char* key = unpack_string(stat);
            char* value = unpack_string(stat);

            if (key && value) {
                prop_len -= strlen(key) + strlen(value) + 4;
                mqtt_user_property(stat, UNSUBACK, key, value);
            } else {
                prop_len -= 4;
            }

            free(key);
            free(value);
        }
        break;

        default:
            return ERROR_UNKNOWN_IDENTIFIER;
        }
    }

    if (prop_len < 0) {
        return ERROR_MALFORMED_PACKET;
    }

    return OK;
}

static int process_unsuback(struct mqtt_client *stat)
{
    int result = OK;

    // Free previously allocated reason string
    if (stat->unsuback.reason_string) {
        free((void*)stat->unsuback.reason_string);
        stat->unsuback.reason_string = NULL;
    }

    // Free previously allocated reason codes
    if (stat->unsuback.reason_codes) {
        free(stat->unsuback.reason_codes);
        stat->unsuback.reason_codes = NULL;
    }

    // Clear UNSUBACK structure
    memset(&stat->unsuback, 0, sizeof(stat->unsuback));

    uint16_t packet_id = unpack_word(stat);
    stat->unsuback.packet_id = packet_id;

    // Verify this packet ID was expected
    mqtt_packet_type expected = get_expected_packet_answer(stat, packet_id);
    if (expected != UNSUBACK) {
        return ERROR_UNEXPECTED_PACKET_TYPE;
    }

    // Process properties
    uint32_t prop_len = unpack_variable_size(stat);
    if (prop_len > 0) {
        result = process_unsuback_properties(stat, prop_len);
        if (FAILED(result)) {
            return result;
        }
    }

    // Process reason codes for each unsubscription
    uint32_t bytes_consumed = stat->pin - (uint8_t*)stat->inp.payload;
    uint32_t remaining_bytes = stat->inp.len - bytes_consumed;

    if (remaining_bytes > 0) {
        stat->unsuback.reason_codes = malloc(remaining_bytes);
        if (!stat->unsuback.reason_codes) {
            return ERROR_OUT_OF_MEMORY;
        }

        stat->unsuback.reason_codes_count = remaining_bytes;

        for (uint32_t i = 0; i < remaining_bytes; i++) {
            stat->unsuback.reason_codes[i] = unpack_byte(stat);
        }
    }

    // Free the packet slot
    free_packet_slot(stat, packet_id);

    // Remove UNSUBACK from expected packet types
    if (!await_for_packet(stat, UNSUBACK)) {
        stat->expected_ptypes &= ~BIT(UNSUBACK);
    }

    return result;
}

static int process_packet(struct mqtt_client *stat, mqtt_packet_type type,
                          uint8_t flags)
{
    switch (type) {
    case CONNACK:
        return process_connack(stat);
    case PUBLISH:
        return process_publish(stat, flags);
    case PUBACK:
        return process_puback(stat);
    case PUBREC:
        return process_pubrec(stat);
    case PUBREL:
        return process_pubrel(stat);
    case PUBCOMP:
        return process_pubcomp(stat);
    case SUBACK:
        return process_suback(stat);
    case UNSUBACK:
        return process_unsuback(stat);
    case DISCONNECT:
        return process_disconnect(stat);
    case PINGREQ:
        return mqtt_ping(stat);
    case PINGRESP:
        mqtt_ping_received(stat);
        return OK;
    default:
        return OK;
    }
}

/***** Final API functions ***********************************************************************/
/*                                                                                               */
/*************************************************************************************************/

char* mqtt_blob_to_string(const struct mqtt_blob* blob)
{
    if (!blob || !blob->data || blob->len == 0) {
        return NULL;
    }

    char* str = malloc(blob->len + 1);
    if (!str) {
        return NULL;
    }

    memcpy(str, blob->data, blob->len);
    str[blob->len] = '\0';

    return str;
}

int mqtt_process_packet(struct mqtt_client *stat, void* data, uint32_t len)
{
    if (data && len) {
        stat->inp.len = len;
        stat->inp.payload = data;
    }
    stat->pin = (uint8_t*) stat->inp.payload;
    uint8_t fixed_header = unpack_byte(stat);
    mqtt_packet_type type = (mqtt_packet_type) fixed_header >> 4;
    uint32_t remaining_len = unpack_variable_size(stat);
    if (remaining_len == (stat->inp.len - get_variable_size_byte_count(remaining_len) - 1)) {
        if (TST(stat->expected_ptypes, BIT(type))) {
            return process_packet(stat, type, fixed_header & 0x0f);
        } else {
            return ERROR_UNEXPECTED_PACKET_TYPE;
        }
    } else {
        return ERROR_INVALID_PACKET_SIZE;
    }
}

int mqtt_poll(struct mqtt_client *stat)
{
    int result = STATUS_PASSED;
    if (stat->net.alloc_recv_buf && stat->net.recv && stat->net.free_recv_buf) {
        stat->net.alloc_recv_buf(stat, &stat->inp,
                                 stat->connack.max_packet_size);
        while (1) {
            result = stat->net.recv(stat, &stat->inp);
            if (stat->inp.len && !result) {
                return mqtt_process_packet(stat, NULL, 0);
            } else {
                break;
            }
        }
        stat->net.free_recv_buf(stat, &stat->inp);
    }
    return result;
}

void mqtt_free_client_strings(struct mqtt_client* stat)
{
    if (!stat) {
        return;
    }

    // Free CONNACK allocated strings
    if (stat->connack.assigned_client_id) {
        free(stat->connack.assigned_client_id);
        stat->connack.assigned_client_id = NULL;
    }
    if (stat->connack.reason_string) {
        free(stat->connack.reason_string);
        stat->connack.reason_string = NULL;
    }
    if (stat->connack.server_reference) {
        free(stat->connack.server_reference);
        stat->connack.server_reference = NULL;
    }
    if (stat->connack.response_info) {
        free(stat->connack.response_info);
        stat->connack.response_info = NULL;
    }

    // Free DISCONNECT allocated strings
    if (stat->disconn.reason_string) {
        free((void*)stat->disconn.reason_string);
        stat->disconn.reason_string = NULL;
    }
    if (stat->disconn.server_reference) {
        free((void*)stat->disconn.server_reference);
        stat->disconn.server_reference = NULL;
    }

    // Free PUBACK allocated strings
    if (stat->puback.reason_string) {
        free((void*)stat->puback.reason_string);
        stat->puback.reason_string = NULL;
    }

    // Free PUBREC allocated strings
    if (stat->pubrec.reason_string) {
        free((void*)stat->pubrec.reason_string);
        stat->pubrec.reason_string = NULL;
    }

    // Free PUBREL allocated strings
    if (stat->pubrel.reason_string) {
        free((void*)stat->pubrel.reason_string);
        stat->pubrel.reason_string = NULL;
    }

    // Free PUBCOMP allocated strings
    if (stat->pubcomp.reason_string) {
        free((void*)stat->pubcomp.reason_string);
        stat->pubcomp.reason_string = NULL;
    }

    // Free RECEIVED_PUBLISH allocated strings
    if (stat->received_publish.topic) {
        free((void*)stat->received_publish.topic);
        stat->received_publish.topic = NULL;
    }
    if (stat->received_publish.response_topic) {
        free((void*)stat->received_publish.response_topic);
        stat->received_publish.response_topic = NULL;
    }
    if (stat->received_publish.content_type) {
        free((void*)stat->received_publish.content_type);
        stat->received_publish.content_type = NULL;
    }

    // Free CONNECT allocated strings (client_id from get_unique_client_id)
    if (stat->connect.client_id) {
        free((void*)stat->connect.client_id);
        stat->connect.client_id = NULL;
    }

    // Free UNSUBACK allocated strings
    if (stat->unsuback.reason_string) {
        free((void*)stat->unsuback.reason_string);
        stat->unsuback.reason_string = NULL;
    }
    if (stat->unsuback.reason_codes) {
        free(stat->unsuback.reason_codes);
        stat->unsuback.reason_codes = NULL;
    }

    // Free the broker address
    if (stat->broker_addr) {
        free(stat->broker_addr);
        stat->broker_addr = NULL;
    }
}

void mqtt_free_client(struct mqtt_client** stat)
{
    if (stat && *stat) {
        mqtt_free_client_strings(*stat);
        free(*stat);
        *stat = NULL;
    }
}

struct mqtt_client* mqtt_create_client(const char* broker_addr)
{
    struct mqtt_client* stat = (struct mqtt_client*) calloc(1, sizeof(struct mqtt_client));
    stat->broker_addr = string_copy(broker_addr);
    mqtt_assign_net_api(stat);
    assert(stat->net.alloc_send_buf);
    assert(stat->net.free_send_buf);
    assert(stat->net.send);
    assert(stat->net.open_conn);
    assert(stat->net.close_conn);
    stat->expected_ptypes = BIT(PINGREQ);
    return stat;
}

int mqtt_connect(struct mqtt_client* stat, uint16_t keep_alive, uint32_t session_expiry, bool clean_start)
{
    int result = validate_connect_utf8_strings(stat);
    if (SUCCESSFUL(result)) {
        stat->connect.keep_alive = keep_alive;
        stat->connect.session_expiry_interval = session_expiry;
        stat->connect.clean_start = clean_start;
        stat->connect.client_id = get_unique_client_id();
        stat->connect.recv_max = MQTT_RECEIVE_MAXIMUM;
        assert(stat->connect.client_id);
        if (!stat->connect.client_id) {
            return ERROR_NULL_REFERENCE;
        }

        // First run estimates the needed memory size
        stat->pout = NULL;
        make_connect(stat);

        // Allocate send buffer
        result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
        if (FAILED(result)) {
            return result;
        }

        // Second run builds the actual packet
        stat->pout = (uint8_t*) stat->outp.payload;
        make_connect(stat);

        // Open connection
        result = stat->net.open_conn(stat, stat->broker_addr);
        if (FAILED(result)) {
            stat->net.free_send_buf(stat, &stat->outp);
            return result;
        }

        // For async net APIs we may not connected here. 
        // In that case we prepare the connect packet and send it later
        if (!stat->net.connected) {
            stat->connect.deferred = true;
        }

        // Send the packet
        result = stat->net.send(stat, &stat->outp);
        if (FAILED(result)) {
            stat->net.free_send_buf(stat, &stat->outp);
            stat->net.close_conn(stat);
            return result;
        }

        // Free send buffer
        if (result != STATUS_PENDING) {
            result = stat->net.free_send_buf(stat, &stat->outp);
            if (FAILED(result)) {
                stat->net.close_conn(stat);
                return result;
            }
        }

        stat->expected_ptypes |= BIT(CONNACK);
    }
    return result;
}

int mqtt_disconnect(struct mqtt_client* stat, mqtt_reason_code reason_code)
{
    int result = validate_disconnect_utf8_strings(stat);
    if (SUCCESSFUL(result)) {
        stat->disconn.reason_code = reason_code;

        // First run estimates the needed memory size
        stat->pout = NULL;
        make_disconnect(stat);

        // Allocate send buffer
        result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
        if (FAILED(result)) {
            return result;
        }

        // Second run builds the actual packet
        stat->pout = (uint8_t*) stat->outp.payload;
        make_disconnect(stat);

        // Send the packet
        result = stat->net.send(stat, &stat->outp);
        if (FAILED(result)) {
            stat->net.free_send_buf(stat, &stat->outp);
            return result;
        }

        // Free send buffer
        result = stat->net.free_send_buf(stat, &stat->outp);
        if (FAILED(result)) {
            // Still close connection even if free fails
            stat->net.close_conn(stat);
            return result;
        }

        // Update state
        stat->connected = false;
        stat->expected_ptypes = BIT(PINGREQ);

        // Close connection
        result = stat->net.close_conn(stat);
    }
    return result;
}

int mqtt_publish(struct mqtt_client* stat, struct mqtt_pub_packet* msg)
{
    if (!stat || !msg) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    // Validate UTF-8 strings
    int result = validate_publish_utf8_strings(stat, msg);
    if (FAILED(result)) {
        return result;
    }

    // Validate QoS level
    if (msg->qos > 2) {
        return ERROR_INVALID_QOS;
    }

    // Check if server supports the requested QoS level
    if (msg->qos > stat->connack.max_qos) {
        return ERROR_QOS_NOT_SUPPORTED;
    }

    // Check if retain is supported
    if (msg->retain && !stat->connack.retain_avail) {
        return ERROR_RETAIN_NOT_SUPPORTED;
    }

    // Validate topic name (no wildcards allowed in publish)
    if (strstr(msg->topic, "+") || strstr(msg->topic, "#")) {
        return ERROR_INVALID_TOPIC;
    }

    // Generate packet identifier for QoS > 0
    if (msg->qos > 0) {
        int packet_id = reserve_packet_slot_for_answer(stat, msg->qos == 2 ? PUBREC : PUBACK);
        if (SUCCESSFUL(packet_id)) {
            msg->packet_id = (uint16_t)packet_id;
        } else {
            return packet_id; // Error out of packet slots
        }
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_publish(stat, msg);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_publish(stat, msg);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types based on QoS
    if (SUCCESSFUL(result)) {
        switch (msg->qos) {
        case 1:
            stat->expected_ptypes |= BIT(PUBACK);
            break;
        case 2:
            stat->expected_ptypes |= BIT(PUBREC);
            break;
        default:
            // QoS 0 - no acknowledgment expected
            break;
        }
    }

    return result;
}

int mqtt_subscribe(struct mqtt_client* stat, struct mqtt_sub_entry* entries, unsigned int entry_count)
{
    if (!stat || !entries || entry_count == 0) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    // Store subscription data
    stat->subscribe.entries = entries;
    stat->subscribe.entry_count = entry_count;

    // Acquire new packet ID with expected answer type
    int packet_id = reserve_packet_slot_for_answer(stat, SUBACK);
    if (SUCCESSFUL(packet_id)) {
        stat->subscribe.packet_id = (uint16_t)packet_id;
    } else {
        return packet_id; // Error out of packet slots
    }

    // Validate UTF-8 strings
    int result = validate_subscribe_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Validate subscription entries
    for (unsigned int i = 0; i < entry_count; i++) {
        if (!entries[i].topic) {
            return ERROR_NULL_REFERENCE;
        }

        // Validate QoS level
        if (entries[i].qos > 2) {
            return ERROR_INVALID_QOS;
        }

        // Check if server supports the requested QoS level
        if (entries[i].qos > stat->connack.max_qos) {
            return ERROR_QOS_NOT_SUPPORTED;
        }

        // Check wildcard subscription support
        if ((strstr(entries[i].topic, "+") || strstr(entries[i].topic, "#")) &&
                !stat->connack.wildcard_sub_avail) {
            return ERROR_UNSUPPORTED;
        }

        // Check shared subscription support
        if (strncmp(entries[i].topic, "$share/", 7) == 0 && !stat->connack.shared_sub_avail) {
            return ERROR_UNSUPPORTED;
        }

        // Validate retain handling
        if (entries[i].retain_handling > 2) {
            return ERROR_UNSUPPORTED;
        }
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_subscribe(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_subscribe(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types
    if (SUCCESSFUL(result)) {
        stat->expected_ptypes |= BIT(SUBACK);
    }

    return result;
}

int mqtt_ping(struct mqtt_client* stat)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_pingreq(stat);

    // Allocate send buffer
    int result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_pingreq(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types to include PINGRESP
    if (SUCCESSFUL(result)) {
        stat->expected_ptypes |= BIT(PINGRESP);
    }

    return result;
}

int mqtt_puback(struct mqtt_client* stat, uint16_t packet_id)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    if (packet_id == 0) {
        return ERROR_INVALID_PACKET_ID;
    }

    // Validate UTF-8 strings in PUBACK properties
    int result = validate_puback_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Store packet ID for PUBACK (reason_code should be set by user before calling this)
    stat->puback.packet_id = packet_id;

    // Set default reason code if not already set
    if (stat->puback.reason_code == 0) {
        stat->puback.reason_code = MQTT_REASON_SUCCESS;
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_puback(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_puback(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    return result;
}

int mqtt_pubrec(struct mqtt_client* stat, uint16_t packet_id)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    if (packet_id == 0) {
        return ERROR_INVALID_PACKET_ID;
    }

    // Validate UTF-8 strings in PUBREC properties
    int result = validate_pubrec_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Store packet ID for PUBREC (reason_code should be set by user before calling this)
    stat->pubrec.packet_id = packet_id;

    // Set default reason code if not already set
    if (stat->pubrec.reason_code == 0) {
        stat->pubrec.reason_code = MQTT_REASON_SUCCESS;
    }

    // Reserve packet slot for expected PUBREL
    result = reserve_packet_slot_for_request(stat, packet_id, PUBREL);
    if (FAILED(result)) {
        return result;
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_pubrec(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_pubrec(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types
    if (SUCCESSFUL(result)) {
        stat->expected_ptypes |= BIT(PUBREL);
    }

    return result;
}

int mqtt_pubrel(struct mqtt_client* stat, uint16_t packet_id)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    if (packet_id == 0) {
        return ERROR_INVALID_PACKET_ID;
    }

    // Validate UTF-8 strings in PUBREL properties
    int result = validate_pubrel_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Store packet ID for PUBREL (reason_code should be set by user before calling this)
    stat->pubrel.packet_id = packet_id;

    // Set default reason code if not already set
    if (stat->pubrel.reason_code == 0) {
        stat->pubrel.reason_code = MQTT_REASON_SUCCESS;
    }

    // Update packet slot to expect PUBCOMP instead of PUBREL
    for (int i = 0; i < MQTT_RECEIVE_MAXIMUM; ++i) {
        if (stat->pending[i].packet_id == packet_id) {
            stat->pending[i].await_packet_type = PUBCOMP;
            break;
        }
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_pubrel(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_pubrel(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types
    if (SUCCESSFUL(result)) {
        stat->expected_ptypes |= BIT(PUBCOMP);
    }

    return result;
}

int mqtt_pubcomp(struct mqtt_client* stat, uint16_t packet_id)
{
    if (!stat) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    if (packet_id == 0) {
        return ERROR_INVALID_PACKET_ID;
    }

    // Validate UTF-8 strings in PUBCOMP properties
    int result = validate_pubcomp_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Store packet ID for PUBCOMP (reason_code should be set by user before calling this)
    stat->pubcomp.packet_id = packet_id;

    // Set default reason code if not already set
    if (stat->pubcomp.reason_code == 0) {
        stat->pubcomp.reason_code = MQTT_REASON_SUCCESS;
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_pubcomp(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_pubcomp(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    return result;
}

int mqtt_unsubscribe(struct mqtt_client* stat, struct mqtt_sub_entry* entries, unsigned int entry_count)
{
    if (!stat || !entries || entry_count == 0) {
        return ERROR_NULL_REFERENCE;
    }

    if (!stat->connected) {
        return ERROR_NOT_CONNECTED;
    }

    // Store unsubscribe data
    stat->unsubscribe.entries = entries;
    stat->unsubscribe.entry_count = entry_count;

    // Acquire new packet ID with expected answer type
    int packet_id = reserve_packet_slot_for_answer(stat, UNSUBACK);
    if (SUCCESSFUL(packet_id)) {
        stat->unsubscribe.packet_id = (uint16_t)packet_id;
    } else {
        return packet_id; // Error out of packet slots
    }

    // Validate UTF-8 strings
    int result = validate_unsubscribe_utf8_strings(stat);
    if (FAILED(result)) {
        return result;
    }

    // Validate unsubscribe entries
    for (unsigned int i = 0; i < entry_count; i++) {
        if (!entries[i].topic) {
            return ERROR_NULL_REFERENCE;
        }

        // Validate topic name (wildcards are allowed in unsubscribe)
        if (!is_valid_utf8(entries[i].topic, strlen(entries[i].topic))) {
            return ERROR_INVALID_ENCODING;
        }
    }

    // First pass: estimate packet size
    stat->pout = NULL;
    make_unsubscribe(stat);

    // Allocate send buffer
    result = stat->net.alloc_send_buf(stat, &stat->outp, stat->packet_size);
    if (FAILED(result)) {
        return result;
    }

    // Second pass: actually build the packet
    stat->pout = (uint8_t*) stat->outp.payload;
    make_unsubscribe(stat);

    // Send the packet
    result = stat->net.send(stat, &stat->outp);

    // Free send buffer
    stat->net.free_send_buf(stat, &stat->outp);

    // Update expected packet types
    if (SUCCESSFUL(result)) {
        stat->expected_ptypes |= BIT(UNSUBACK);
    }

    return result;
}