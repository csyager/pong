#include <stdio.h>
#include <sys/socket.h>
#include <string.h>

#include "config.h"
#include "protocol.h"
#include "game.h"

void tick(union sigval sv) {
	
	TickState *tick_state = (TickState *)sv.sival_ptr;
	struct timespec now;
	time_t wall_now = time(NULL);
	clock_gettime(CLOCK_MONOTONIC, &now);
	
	// don't start the game until all clients are connected
	if (!tick_state->game_started && tick_state->scheduled_start == 0) {
		for (int i = 0; i < MAX_CLIENTS; i++) {
			Client* c = &tick_state->clients[i];
			if (!c->active) {
				printf("Waiting on all clients.  Only %d clients connected.\n", i);
				tick_state->latest_tick = now;
				return;
			}
		}
		// all clients connected, schedule game start!
		time_t start_time = time(NULL) + 5;
		tick_state->scheduled_start = start_time; 

		// send start message via tcp
		struct TcpMessage startGameMessage;
		startGameMessage.opcode = 1;
		char buffer[256 + sizeof(uint32_t)];
		memset(buffer, 0, sizeof(buffer));
		serialize_tcp_message(&startGameMessage, buffer); 

		for (int i = 0; i < MAX_CLIENTS; i++) {
			Client* c = &tick_state->clients[i];

			if (!c->active)
				continue;

			size_t total_sent = 0;
			ssize_t bytes_sent;

			while (total_sent < sizeof(buffer)) {
				bytes_sent = send(
					tick_state->clients[i].tcp_fd,
					&buffer + total_sent,
					sizeof(buffer) - total_sent,
					0
				);
				if (bytes_sent < 0)
					perror("send");
				total_sent += bytes_sent;
			}

			printf("Sent %ld bytes to client %d to start game.\n", total_sent, i);
		}
	} else if (!tick_state->game_started && tick_state->scheduled_start != 0) {
		// start the game if the scheduled start has elapsed
		if (wall_now >= tick_state->scheduled_start)
			tick_state->game_started = true;
	} else {
		printf("Moving ball...\n");
		// game is running, move the ball!
		double time_delta = (now.tv_sec - tick_state->latest_tick.tv_sec) +
			(now.tv_nsec - tick_state->latest_tick.tv_nsec) / 1e9;

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
	}


	GameStateMessage message;
	message.seconds_to_start = (int32_t)tick_state->scheduled_start - wall_now;
	message.num_positions = MAX_CLIENTS + 1;
	Position ballPosition = *tick_state->ball_position;
	message.positions[0] = ballPosition;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		message.positions[i + 1] = tick_state->player_positions[i];
	}

	uint8_t buffer[256];
	serialize_game_state_message(buffer, &message);

	for (int i = 0; i < MAX_CLIENTS; i++) {
		Client* client = &tick_state->clients[i];

		if (!client->active)
			continue;

		ssize_t sent = sendto(
			tick_state->udp_sock_fd,
			buffer,
			sizeof(buffer),
			0,
			(struct sockaddr*)&client->addr,
			sizeof(client->addr)
		);

		if (sent < 0)
			perror("sendto");
		printf("sent %lu bytes to client %d for game state \n", sent, i);
	}

	tick_state->latest_tick = now;

}
