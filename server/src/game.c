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
	if (!tick_state->game_active && tick_state->scheduled_start == 0) {
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

	} else if (!tick_state->game_active && tick_state->scheduled_start != 0) {
		// start the game if the scheduled start has elapsed
		if (wall_now >= tick_state->scheduled_start)
			tick_state->game_active = true;
	} else {
		// game is running, move the ball!
		double time_delta = (now.tv_sec - tick_state->latest_tick.tv_sec) +
			(now.tv_nsec - tick_state->latest_tick.tv_nsec) / 1e9;

		tick_state->ball_position->x += tick_state->ball_position->dx * time_delta;
		tick_state->ball_position->y += tick_state->ball_position->dy * time_delta;

		// left and right wall collisions - change score and reset
		if (tick_state->ball_position->x - BALL_RADIUS <= 0.0) {
			tick_state->right_score += 1;
			reset_game(tick_state);
		} else if (tick_state->ball_position->x + BALL_RADIUS > COLS) {
			tick_state->left_score += 1;
			reset_game(tick_state);
		}

		// top and bottom wall collisions
		if (tick_state->ball_position->y - BALL_RADIUS <= 0.0) {
			tick_state->ball_position->y = BALL_RADIUS;
			tick_state->ball_position->dy *= -1;
		} else if (tick_state->ball_position->y + BALL_RADIUS > ROWS) {
			tick_state->ball_position->y = ROWS - BALL_RADIUS;
			tick_state->ball_position->dy *= -1;
		}

		// player collisions
		for (int i = 0; i < MAX_CLIENTS; i++) {
			float px = tick_state->player_positions[i].x;
			float py = tick_state->player_positions[i].y;
			float bx = tick_state->ball_position->x;
			float by = tick_state->ball_position->y;

			// check if ball overlaps the paddle rectangle
			bool overlap_x = (bx + BALL_RADIUS >= px) && (bx - BALL_RADIUS <= px + PLAYER_LENGTH);
			bool overlap_y = (by + BALL_RADIUS >= py) && (by - BALL_RADIUS <= py + PLAYER_LENGTH);

			if (overlap_x && overlap_y) {
				// determine which side of the paddle the ball is closer to
				float paddle_center_x = px + PLAYER_LENGTH / 2.0f;
				if (bx < paddle_center_x) {
					// ball hit left side, push it left and ensure dx goes left
					tick_state->ball_position->x = px - BALL_RADIUS;
					if (tick_state->ball_position->dx > 0)
						tick_state->ball_position->dx *= -1;
				} else {
					// ball hit right side, push it right and ensure dx goes right
					tick_state->ball_position->x = px + PLAYER_LENGTH + BALL_RADIUS;
					if (tick_state->ball_position->dx < 0)
						tick_state->ball_position->dx *= -1;
				}
			}
		}
	}


	GameStateMessage message;
	message.left_score = tick_state->left_score;
	message.right_score = tick_state->right_score;
	message.game_active = tick_state->game_active;
	message.seconds_to_start = (int32_t)tick_state->scheduled_start - wall_now;
	message.num_positions = MAX_CLIENTS + 1;
	Position ballPosition = *tick_state->ball_position;
	message.positions[0] = ballPosition;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		message.positions[i + 1] = tick_state->player_positions[i];
	}

	uint8_t buffer[256];
	serialize_game_state_message(buffer, &message);

	// broadcast game state to clients
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

void reset_game(TickState *tick_state) {
	tick_state->game_active = false;
	time_t start_time = time(NULL) + 5;
	tick_state->scheduled_start = start_time; 

	tick_state->ball_position->x = COLS / 2.0;
	tick_state->ball_position->y = ROWS / 2.0;
}
