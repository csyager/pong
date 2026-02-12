#include <stdio.h>
#include <sys/socket.h>

#include "config.h"
#include "game.h"

void tick(union sigval sv) {
	TickState *tick_state = (TickState *)sv.sival_ptr;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	double time_delta = (now.tv_sec - tick_state->latest_tick.tv_sec) +
		(now.tv_nsec - tick_state->latest_tick.tv_nsec) / 1e9;

	//printf("Time elapsed between ticks: %.2f seconds\n", time_delta);

	tick_state->ball_position->x += tick_state->ball_position->dx * time_delta;
	tick_state->ball_position->y += tick_state->ball_position->dy * time_delta;

	if (tick_state->ball_position->x - BALL_RADIUS <= 0.0) {
		tick_state->ball_position->x = BALL_RADIUS;
		tick_state->ball_position->dx *= -1;
	} else if (tick_state->ball_position->x + BALL_RADIUS > COLS) {
		tick_state->ball_position->x = COLS - BALL_RADIUS;
		tick_state->ball_position->dx *= -1;
	}
	if (tick_state->ball_position->y - BALL_RADIUS <= 0.0) {
		tick_state->ball_position->y = BALL_RADIUS;
		tick_state->ball_position->dy *= -1;
	} else if (tick_state->ball_position->y + BALL_RADIUS > ROWS) {
		tick_state->ball_position->y = ROWS - BALL_RADIUS;
		tick_state->ball_position->dy *= -1;
	}

	// printf("New ball position: (%f, %f)\n", tick_state->ball_position->x, tick_state->ball_position->y);

	struct PositionMessage msg;
	// htonl converts unint32_t to network-byte order (i.e., big-endian)
	msg.id = 0;
	msg.position = *tick_state->ball_position;

	uint8_t message_buffer[20];

	serialize_position_message(&msg, message_buffer);

	for (int i = 0; i < MAX_CLIENTS; i++) {
		Client* c = &tick_state->clients[i];

		if (!c->active)
			continue;

		ssize_t sent = sendto(
			tick_state->udp_sock_fd,
			&message_buffer,
			sizeof(message_buffer),
			0,
			(struct sockaddr*)&c->addr,
			sizeof(c->addr)
		);

		if (sent < 0)
			perror("sendto");

	}

	// broadcast every player position to all other clients
	for (int i = 0; i < MAX_CLIENTS; i++) {
		Client* broadcaster = &tick_state->clients[i];

		if (!broadcaster->active)
			continue;

		for (int j = 0; j < MAX_CLIENTS; j++) {
			if (j == i)
				continue;
			Client* receiver = &tick_state->clients[j];
			if (!receiver->active)
				continue;

			struct PositionMessage player_msg;
			player_msg.id = j + 1;
			player_msg.position = tick_state->player_positions[j];
			uint8_t player_message_buffer[20];

			serialize_position_message(&player_msg, player_message_buffer);
			ssize_t sent = sendto(
				tick_state->udp_sock_fd,
				&player_message_buffer,
				sizeof(player_message_buffer),
				0,
				(struct sockaddr*)&broadcaster->addr,
				sizeof(broadcaster->addr)
			);

			if (sent < 0)
				perror("sendto");

			printf("sent %lu bytes to client %d for player %d's position \n", sent, j + 1, i + 1);
		}
	}

	tick_state->latest_tick = now;

}
