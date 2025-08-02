/**
 * @file mqtt.h
 * @author Pierre Biermann (https://github.com/ByteBender747)
 * @brief MQTT Library Header
 * @version 0.1
 * @date 2025-06-30
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef MQTT_H_INCLUDED
#define MQTT_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#include "mqtt_types.h"
#include "mqtt_const.h"

#define SUB(_topic, _qos) \
    { .topic = _topic, .qos = _qos, .retain_as_published = 1 }

#define SUBSCRIPTION_LIST_BEGIN(name) \
    struct mqtt_sub_entry name[] = {

#define SUBSCRIPTION_LIST_END };

typedef struct mqtt_client mqtt_client_t;
typedef struct mqtt_pub_packet mqtt_pub_packet_t;
typedef struct mqtt_user_property mqtt_user_property_t;
typedef struct mqtt_sub_entry mqtt_sub_entry_t;
typedef struct mqtt_blob mqtt_blob_t;

/***** Inline functions **************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

/**
 * @brief Set the maximum packet size for the MQTT connection
 * 
 * This function sets the maximum packet size that the client is willing to accept.
 * The server will not send packets larger than this size.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param size Maximum packet size in bytes
 */
static inline void mqtt_set_maximum_packet_size(struct mqtt_client *stat,
                                                uint32_t size)
{
    stat->connect.max_packet_size = size;
}

/**
 * @brief Set basic authentication credentials for the MQTT connection
 * 
 * This function enables username/password authentication and sets the credentials
 * that will be used during the CONNECT operation.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param user Username string (must remain valid during connection)
 * @param passwd Password string (must remain valid during connection)
 */
static inline void mqtt_set_basic_auth(struct mqtt_client *stat, const char *user,
                                       const char *passwd)
{
    stat->connect.pw_flag = true;
    stat->connect.un_flag = true;
    stat->connect.user = user;
    stat->connect.passwd = passwd;
}

/**
 * @brief Check if the MQTT client is currently connected
 * 
 * @param stat Pointer to the MQTT client structure
 * @return true if connected, false otherwise
 */
static inline bool mqtt_is_connected(struct mqtt_client *stat)
{
    return stat && stat->connected;
}

/**
 * @brief Create a publish packet structure
 * 
 * This is a convenience function to create a properly initialized mqtt_pub_packet
 * structure with the specified parameters.
 * 
 * @param topic Topic name to publish to (must remain valid during publish)
 * @param payload Pointer to payload data (must remain valid during publish)
 * @param len Length of payload data in bytes
 * @param qos Quality of Service level (0, 1, or 2)
 * @param retain Whether to set the retain flag
 * @return Initialized mqtt_pub_packet structure
 */
static inline struct mqtt_pub_packet mqtt_pub_packet(const char *topic,
                                                     void *payload,
                                                     uint32_t len, uint8_t qos,
                                                     bool retain)
{
    struct mqtt_pub_packet p = {.topic = topic,
                                .payload.data = payload,
                                .payload.len = len,
                                .qos = qos,
                                .retain = retain};
    return p;
}

/***** Prototypes ********************************************************************************/
/*                                                                                               */
/*************************************************************************************************/

/**
 * @brief Convert mqtt_blob to a null-terminated string
 * @param blob Pointer to the mqtt_blob structure
 * @return Dynamically allocated string (caller must free) or NULL on error
 */
char* mqtt_blob_to_string(const struct mqtt_blob* blob);

/**
 * @brief Create a new MQTT client instance
 * 
 * Allocates and initializes a new MQTT client structure with the specified broker address.
 * The client must be freed using mqtt_free_client() when no longer needed.
 * 
 * @param broker_addr IP address or hostname of the MQTT broker
 * @return Pointer to the newly created MQTT client, or NULL on failure
 */
struct mqtt_client* mqtt_create_client(const char* broker_addr);

/**
 * @brief Connect to the MQTT broker
 * 
 * Establishes a connection to the MQTT broker with the specified parameters.
 * This function will send a CONNECT packet and expect a CONNACK response.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param keep_alive Keep alive interval in seconds (0 to disable)
 * @param session_expiry Session expiry interval in seconds (0 for session ends at disconnect)
 * @param clean_start Whether to start a clean session
 * @return Status code indicating success or failure
 */
int mqtt_connect(struct mqtt_client* stat, uint16_t keep_alive, uint32_t session_expiry, bool clean_start);

/**
 * @brief Disconnect from the MQTT broker
 * 
 * Sends a DISCONNECT packet to the broker and closes the connection gracefully.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param reason_code Reason code for the disconnection
 * @return Status code indicating success or failure
 */
int mqtt_disconnect(struct mqtt_client* stat, mqtt_reason_code reason_code);

/**
 * @brief Process an incoming MQTT packet
 * 
 * Parses and processes an MQTT packet received from the broker. This function
 * handles all incoming packet types and updates the client state accordingly.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param data Pointer to the packet data (NULL to use internal buffer)
 * @param len Length of the packet data in bytes (0 to use internal buffer)
 * @return Status code indicating success or failure
 */
int mqtt_process_packet(struct mqtt_client *stat, void* data, uint32_t len);

/**
 * @brief Poll for incoming MQTT packets
 * 
 * Checks for incoming packets from the broker and processes them if available.
 * This function should be called regularly in the main loop.
 * 
 * @param stat Pointer to the MQTT client structure
 * @return Status code indicating success or failure
 */
int mqtt_poll(struct mqtt_client *stat);

/**
 * @brief Subscribe to one or more MQTT topics
 * 
 * Sends a SUBSCRIBE packet to the broker to request subscription to the specified topics.
 * The broker will respond with a SUBACK packet containing the granted QoS levels.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param entries Array of subscription entries specifying topics and options
 * @param entry_count Number of entries in the subscription array
 * @return Status code indicating success or failure
 */
int mqtt_subscribe(struct mqtt_client* stat, struct mqtt_sub_entry* entries, unsigned int entry_count);

/**
 * @brief Unsubscribe from one or more MQTT topics
 * 
 * Sends an UNSUBSCRIBE packet to the broker to cancel subscription to the specified topics.
 * The broker will respond with an UNSUBACK packet containing reason codes for each topic.
 * Wildcards are allowed in topic filters for unsubscribe operations.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param entries Array of unsubscribe entries specifying topics to unsubscribe from
 * @param entry_count Number of entries in the unsubscribe array
 * @return Status code indicating success or failure
 */
int mqtt_unsubscribe(struct mqtt_client* stat, struct mqtt_sub_entry* entries, unsigned int entry_count);

/**
 * @brief Publish a message to the MQTT broker
 * 
 * Sends a PUBLISH packet with the specified message to the broker.
 * For QoS > 0, the function will handle acknowledgment packets automatically.
 * 
 * @param stat Pointer to the MQTT client structure
 * @param msg Pointer to the publish packet structure containing message details
 * @return Status code indicating success or failure
 */
int mqtt_publish(struct mqtt_client* stat, struct mqtt_pub_packet* msg);

/**
 * @brief Send a ping request to the MQTT broker
 * 
 * Sends a PINGREQ packet to the broker to keep the connection alive.
 * The broker will respond with a PINGRESP packet.
 * 
 * @param stat Pointer to the MQTT client structure
 * @return Status code indicating success or failure
 */
int mqtt_ping(struct mqtt_client* stat);

/**
 * @brief Free all dynamically allocated strings in the MQTT client
 * 
 * Frees all strings that were allocated during the client's operation,
 * such as received strings from broker responses. This function is automatically
 * called by mqtt_free_client() but can be called separately if needed.
 * 
 * @param stat Pointer to the MQTT client structure
 */
void mqtt_free_client_strings(struct mqtt_client* stat);

/**
 * @brief Free the MQTT client and all associated resources
 * 
 * Frees the MQTT client structure and all associated resources, including
 * dynamically allocated strings. Sets the pointer to NULL to prevent reuse.
 * 
 * @param stat Pointer to the MQTT client pointer (will be set to NULL)
 */
void mqtt_free_client(struct mqtt_client** stat);

#endif /* MQTT_H_INCLUDED */