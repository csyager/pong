#ifndef GAME_H
#define GAME_H

#include <time.h>
#include <signal.h>

#include "protocol.h"

typedef struct {
	Position* ball_position;
	Position* player_positions;
	int udp_sock_fd;
	int tcp_sock_fd;
	struct timespec latest_tick;
	Client* clients;

	bool game_started;
	time_t scheduled_start;
} TickState;

void tick(union sigval sv);

#endif
