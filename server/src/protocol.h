#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "config.h"

typedef struct {
	struct sockaddr_in addr;
	uint32_t tcp_port;
	int tcp_fd;
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

/**
 * structure broadcasted from clients to server containing position info
 */
struct __attribute((packed)) PositionMessage {
	uint32_t id;
	Position position;
};

/**
 * Structrue broadcasted from server to clients containing game state info
 */
typedef struct __attribute((packed)) {
	uint8_t left_score;
	uint8_t right_score;
	bool game_active;
	int32_t seconds_to_start;
	uint32_t num_positions;
	Position positions[MAX_CLIENTS + 1];  //position for each player, plus the ball
} GameStateMessage;

typedef struct __attribute((packed)) {
	uint16_t udp_port;
	uint16_t tcp_port;
} ClientNetworkingData;

typedef struct __attribute((packed)) {
	time_t scheduled_start;
} StartGameMessage;

struct __attribute((packed)) TcpMessage {
	uint32_t opcode;
	char msg[256];
};

struct __attribute((packed)) TcpResponse {
	uint32_t statuscode;
	char msg[256];
};

void serialize_position_message(const struct PositionMessage* msg, uint8_t* buffer);
void serialize_tcp_message(const struct TcpMessage* tcpMessage, char* buffer);
void deserialize_position_message(const uint8_t* buffer, struct PositionMessage* msg);
void deserialize_tcp_message(char buffer[256], struct TcpMessage* msg);
void serialize_tcp_response(const struct TcpResponse* tcpResponse, uint8_t* buffer);

void serialize_game_state_message(uint8_t* buffer, const GameStateMessage* gameStateMessage);

#endif
