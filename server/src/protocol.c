/*
 * protocol.c -- models and serialization/deserialization for networking
 */

#include <string.h>
#include <arpa/inet.h>

#include "protocol.h"

void serialize_position_message(const struct PositionMessage* msg, uint8_t* buffer) {
	uint32_t id = htonl(msg->id);
	memcpy(buffer, &id, 4);

	// serialize floats by bit-casting to uint32_t first
	float fields[4] = {msg->position.x, msg->position.y, msg->position.dx, msg->position.dy};
	for (int i = 0; i < 4; i++) {
		uint32_t temp;
		memcpy(&temp, &fields[i], 4);
		uint32_t net_val = htonl(temp);
		memcpy(buffer + 4 + (i * 4), &net_val, 4);
	}
}

void deserialize_position_message(const uint8_t* buffer, struct PositionMessage* msg) {
	uint32_t temp_val;
	memcpy(&temp_val, buffer, 4);
	msg->id = ntohl(temp_val);

	uint32_t host_bits;
	int offset = 4;

	memcpy(&temp_val, buffer + offset, 4);
	host_bits = ntohl(temp_val);
	memcpy(&msg->position.x, &host_bits, 4);
	offset += 4;

	// y
	memcpy(&temp_val, buffer + offset, 4);
	host_bits = ntohl(temp_val);
	memcpy(&msg->position.y, &host_bits, 4);
	offset += 4;

	// dx
	memcpy(&temp_val, buffer + offset, 4);
	host_bits = ntohl(temp_val);
	memcpy(&msg->position.dx, &host_bits, 4);
	offset += 4;

	// dy
	memcpy(&temp_val, buffer + offset, 4);
	host_bits = ntohl(temp_val);
	memcpy(&msg->position.dy, &host_bits, 4);

}

/**
 * Serialize TcpMessage into a buffer, to be sent over the wire
 */
void serialize_tcp_message(const struct TcpMessage* tcpMessage, char* buffer) {
	memset(buffer, 0, sizeof(*buffer));
	uint32_t opcode = htonl(tcpMessage->opcode);
	memcpy(buffer, &opcode, sizeof(uint32_t));

	int offset = sizeof(uint32_t);
	memcpy(buffer + offset, tcpMessage->msg, sizeof(tcpMessage->msg));
}


void deserialize_tcp_message(char buffer[256], struct TcpMessage* msg) {
	uint32_t temp_val;
	memcpy(&temp_val, buffer, 4);
	msg->opcode = ntohl(temp_val);
}

void serialize_game_state_message(uint8_t* buffer, const GameStateMessage* gameStateMessage) {
	int offset = 0;

	uint32_t start_time = htonl((uint32_t)gameStateMessage->start_time);
	memcpy(buffer + offset, &start_time, 4);
	offset += 4;

	uint32_t num_positions = htonl(gameStateMessage->num_positions);
	memcpy(buffer + offset, &num_positions, 4);
	offset += 4;

	for (int i = 0; i < (int)gameStateMessage->num_positions; i++) {
		float fields[4] = {
			gameStateMessage->positions[i].x,
			gameStateMessage->positions[i].y,
			gameStateMessage->positions[i].dx,
			gameStateMessage->positions[i].dy
		};
		for (int j = 0; j < 4; j++) {
			uint32_t temp;
			memcpy(&temp, &fields[j], 4);
			uint32_t net_val = htonl(temp);
			memcpy(buffer + offset, &net_val, 4);
			offset += 4;
		}
	}
}

void serialize_tcp_response(const struct TcpResponse* tcpResponse, uint8_t* buffer) {
	uint32_t response_code = htonl(tcpResponse->statuscode);
	memcpy(buffer, &response_code, 4);
	size_t offset = 4;

	memcpy(buffer + offset, tcpResponse->msg, sizeof(tcpResponse->msg));
}
