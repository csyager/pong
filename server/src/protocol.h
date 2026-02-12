#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <netinet/in.h>
#include <stdbool.h>

typedef struct {
	struct sockaddr_in addr;
	uint32_t tcp_port;
	uint32_t udp_port;
	uint32_t player_id;
	bool active;
} Client;

typedef struct __attribute((packed)) {
	float x;
	float y;
	float dx;
	float dy;
} Position;

struct __attribute((packed)) PositionMessage {
	uint32_t id;
	Position position;
};

typedef struct __attribute((packed)) {
	uint16_t udp_port;
	uint16_t tcp_port;
} ClientNetworkingData;

struct __attribute((packed)) TcpMessage {
	uint32_t opcode;
	ClientNetworkingData clientNetworkingData;
};

struct __attribute((packed)) TcpResponse {
	uint32_t statuscode;
	char msg[256];
};

void serialize_position_message(const struct PositionMessage* msg, uint8_t* buffer);
void deserialize_position_message(const uint8_t* buffer, struct PositionMessage* msg);
void deserialize_tcp_message(char buffer[256], struct TcpMessage* msg);
void serialize_tcp_response(const struct TcpResponse* tcpResponse, uint8_t* buffer);

#endif
