# MQLite - Lightweight MQTT 5.0 Client Library

A lightweight C library implementing MQTT 5.0 protocol for embedded systems and desktop applications.

## Features

- **MQTT 5.0 Support**: Full implementation of MQTT 5.0 protocol
- **Dual Network Backend**: Support for both standard sockets and LwIP (for Raspberry Pi Pico W)
- **Embedded-Friendly**: Minimal memory footprint and resource usage
- **Quality of Service**: Support for QoS 0, 1, and 2 message delivery
- **Authentication**: Basic username/password authentication
- **Clean API**: Simple and intuitive C API

## Building

### Prerequisites

- CMake 3.15 or higher
- C11 compatible compiler (GCC, clang, MS Visual C not yet tested probably not working)

### Build (Linux/macOS/Windows/PICO)

MQLite is intended to be used as a static link library. Simply copy the entire folder into your project root directory and add the following lines to your main CMakeLists.txt.: 

```
add_subdirectory(mqlite)
target_link_libraries(${PROJECT_NAME} PRIVATE mqlite)
```

For Raspberry PICO it will automatically be compiled as an interface library just like other libraries from the PICO SDK.

### Build Options

The library automatically detects the target platform:
- **Standard platforms**: Uses socket-based networking (`mqtt_socket.c`)
- **Raspberry Pi Pico W**: Uses LwIP networking (`mqtt_lwip.c`)

## Usage

### Basic Client Setup

```c
#include "mqtt.h"

// Create a client
struct mqtt_client* client = mqtt_create_client("192.168.1.100");

// Optional: Set authentication
mqtt_set_basic_auth(client, "username", "password");

// Optional: Set maximum packet size
mqtt_set_maximum_packet_size(client, 1024);

// Connect to broker
int result = mqtt_connect(client, 60, 0, true);
if (result != 0) {
    // Handle connection error
}
```

### Publishing Messages

```c
// Create a publish packet
struct mqtt_pub_packet msg = mqtt_pub_packet(
    "sensors/temperature",  // topic
    "23.5",                 // payload
    4,                      // payload length
    0,                      // QoS
    false                   // retain
);

// Publish the message
int result = mqtt_publish(client, &msg);
```

### Subscribing to Topics

```c
// Define subscriptions using convenience macros
SUBSCRIPTION_LIST_BEGIN(subscriptions)
    SUB("sensors/+", 0),
    SUB("commands/device1", 1)
SUBSCRIPTION_LIST_END

// Subscribe to topics
int result = mqtt_subscribe(client, subscriptions, 
                           sizeof(subscriptions) / sizeof(subscriptions[0]));
```

### Processing Messages

```c
// Main loop
while (mqtt_is_connected(client)) {
    // Poll for incoming messages
    int result = mqtt_poll(client);
    
    if (client->message_available) {
        // Access received message
        printf("Topic: %s\n", client->received_publish.topic);
        printf("Payload: %.*s\n", 
               client->received_publish.payload.len,
               (char*)client->received_publish.payload.data);
        
        client->message_available = false;
    }
    
    // Send periodic ping
    mqtt_ping(client);
    
    // Small delay
    usleep(10000);
}
```

### Complete Example

```c
#include "mqtt.h"
#include <stdio.h>
#include <unistd.h>

int main() {
    // Create and configure client
    struct mqtt_client* client = mqtt_create_client("test.mosquitto.org");
    mqtt_set_basic_auth(client, "testuser", "testpass");
    
    // Connect
    if (mqtt_connect(client, 60, 0, true) != 0) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }
    
    // Subscribe to topic
    SUBSCRIPTION_LIST_BEGIN(subs)
        SUB("test/topic", 0)
    SUBSCRIPTION_LIST_END
    
    mqtt_subscribe(client, subs, sizeof(subs) / sizeof(subs[0]));
    
    // Publish a message
    struct mqtt_pub_packet msg = mqtt_pub_packet("test/topic", "Hello World!", 12, 0, false);
    mqtt_publish(client, &msg);
    
    // Message loop
    for (int i = 0; i < 100; i++) {
        mqtt_poll(client);
        
        if (client->message_available) {
            printf("Received: %.*s\n", 
                   client->received_publish.payload.len,
                   (char*)client->received_publish.payload.data);
            client->message_available = false;
        }
        
        usleep(100000); // 100ms
    }
    
    // Clean disconnect
    mqtt_disconnect(client, MQTT_REASON_NORMAL_DISCONNECTION);
    mqtt_free_client(&client);
    
    return 0;
}
```

## API Reference

### Client Management

- `mqtt_create_client(broker_addr)` - Create new client instance
- `mqtt_free_client(client)` - Free client and resources
- `mqtt_is_connected(client)` - Check connection status

### Connection Management

- `mqtt_connect(client, keep_alive, session_expiry, clean_start)` - Connect to broker
- `mqtt_disconnect(client, reason_code)` - Disconnect from broker
- `mqtt_ping(client)` - Send ping request

### Message Processing

- `mqtt_poll(client)` - Poll for incoming messages (not used for LwIP)
- `mqtt_process_packet(client, data, len)` - Process specific packet

### Publishing

- `mqtt_publish(client, packet)` - Publish message
- `mqtt_pub_packet(topic, payload, len, qos, retain)` - Create publish packet

### Subscriptions

- `mqtt_subscribe(client, entries, count)` - Subscribe to topics
- `mqtt_unsubscribe(client, entries, count)` - Unsubscribe from topics

### Configuration

- `mqtt_set_basic_auth(client, username, password)` - Set authentication
- `mqtt_set_maximum_packet_size(client, size)` - Set max packet size

## Callback Functions

The library provides several weak callback functions that can be overridden in your application to handle specific MQTT events:

```c
// Called when a user property is received in any MQTT packet
void mqtt_user_property(struct mqtt_client* stat, mqtt_packet_type origin, 
                       const char* key, const char* value);

// Called when a PUBLISH message is received
void mqtt_received_publish(struct mqtt_client* stat);

// Called when a subscription is declined by the broker
void mqtt_subscription_declined(struct mqtt_client* stat, uint16_t packet_id, 
                               int num, uint8_t reason_code);

// Called when a subscription is granted by the broker
void mqtt_subscription_granted(struct mqtt_client* stat, uint16_t packet_id, int num);

// Called when a DISCONNECT message is received from the broker
void mqtt_received_disconnect(struct mqtt_client* stat, mqtt_reason_code reason_code);

// Called when a PING response is received from the broker
void mqtt_ping_received(struct mqtt_client* stat);

// Called when a PUBLISH message is acknowledged (QoS 1)
void mqtt_publish_acknowledged(struct mqtt_client* stat, uint16_t packet_id, 
                              uint8_t reason_code);

// Called when a PUBLISH message is completed (QoS 2)
void mqtt_publish_completed(struct mqtt_client* stat, uint16_t packet_id, 
                           uint8_t reason_code);
```

### Using Callbacks

To use these callbacks, simply implement them in your application code:

```c
#include "mqtt.h"

// Override the publish callback
void mqtt_received_publish(struct mqtt_client* stat) {
    printf("Received message on topic: %s\n", stat->received_publish.topic);
    printf("Payload: %.*s\n", 
           stat->received_publish.payload.len,
           (char*)stat->received_publish.payload.data);
}

// Override the subscription granted callback
void mqtt_subscription_granted(struct mqtt_client* stat, uint16_t packet_id, int num) {
    printf("Subscription %d granted for packet %d\n", num, packet_id);
}

// Override the disconnect callback
void mqtt_received_disconnect(struct mqtt_client* stat, mqtt_reason_code reason_code) {
    printf("Disconnected with reason: 0x%02X\n", reason_code);
}
```

**Note:** These callbacks are defined as weak functions, meaning they have default empty implementations that can be overridden by your code without causing linker conflicts.

## Configuration

The library can be configured through compile-time definitions:

```c
#define MQTT_RECEIVE_MAXIMUM 32           // Max concurrent QoS 1/2 messages
#define MQTT_CORELATION_DATA_MAXIMUM 256  // Max correlation data size
#define MQTT_PORT 1883                    // Default MQTT port
#define MQTT_POLL_TIMEOUT 250             // Poll timeout in milliseconds
```

## Platform Support

- **Linux/macOS/Windows**: Standard socket implementation
- **Raspberry Pi Pico W**: LwIP implementation with WiFi support
- **Other embedded platforms**: Extensible through network API abstraction

## License

MIT License

Copyright (c) 2025 Pierre Biermann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Version

Current version: 0.0.1