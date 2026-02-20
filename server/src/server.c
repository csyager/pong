/*
 * server.c -- game server for terminal-based game
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

#include "config.h"
#include "protocol.h"
#include "game.h"

// get sockaddr in IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
	printf("Starting the game server.\n");

	// Seed the random number generator once
	srand(time(NULL));

	// INIT PHYSICS ================================
	Position ballPosition;
	ballPosition.x = COLS / 2.0;
	ballPosition.y = ROWS / 2.0;

	double speed = BALL_MIN_STARTING_VELO + ((double)rand() / RAND_MAX) * (BALL_MAX_STARTING_VELO - BALL_MIN_STARTING_VELO);
	ballPosition.dx = (rand() % 2 == 0) ? speed : -speed;
	speed = BALL_MIN_STARTING_VELO + ((double)rand() / RAND_MAX) * (BALL_MAX_STARTING_VELO - BALL_MIN_STARTING_VELO);
	ballPosition.dy = (rand() % 2 == 0) ? speed : -speed;



	// TCP NETWORKING ============================
	fd_set master;		// master file descriptor list
	fd_set read_fds;	// temp file descriptor list for select()
	int fdmax;		// largest file descriptor

	Client clients[MAX_CLIENTS];
	memset(clients, 0, sizeof(clients));

	Position player_positions[MAX_CLIENTS];
	memset(player_positions, 0, sizeof(player_positions));

	int tcp_listener, udp_listener;		// FD for the server listener
	int newfd;		// newly accepted fd
	struct sockaddr_storage remoteaddr;	//client address
	socklen_t addrlen;

	char buf[260];		// buffer for client data, 260 = 4 bit opcode + 256 bit buffer
	int nbytes;

	char remoteIP[INET6_ADDRSTRLEN];

	int yes=1;
	int i, j, rv;

	struct addrinfo hints, *ai, *p;

	FD_ZERO(&master);	// clear the master and temp sets
	FD_ZERO(&read_fds);

	// get a socket and bind it
	// the server will listen on this socket for connections and data
	printf("Initializing TCP networking.\n");
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	// get socket address info for the listener, store in ai
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "server: %s\n", gai_strerror(rv));
		exit(1);
	}

	// get socket for TCP listener
	for (p = ai; p != NULL; p = p->ai_next) {
		tcp_listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (tcp_listener < 0) {
			continue;
		}

		setsockopt(tcp_listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(tcp_listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(tcp_listener);
			continue;
		}
		break;
	}

	// UDP NETWORKING ===============================
	printf("Initializing UDP networking.\n");
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	// get socket address info for udp listener
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "server: %s\n", gai_strerror(rv));
		exit(1);
	}

	// get socket for UDP listener
	for (p = ai; p != NULL; p = p->ai_next) {
		udp_listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (udp_listener < 0) {
			continue;
		}

		setsockopt(udp_listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		setsockopt(udp_listener, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));

		if (bind(udp_listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(udp_listener);
			continue;
		}
		break;
	}

	// if we got here then we didn't get bound
	if (p == NULL) {
		fprintf(stderr, "selectserver: failed to bind\n");
		exit(2);
	}

	freeaddrinfo(ai);


	// listen on TCP listener socket
	printf("Staring TCP listener.\n");
	if (listen(tcp_listener, 10) == -1) {
		perror("listen");
		exit(3);
	}

	// add listener to master set
	FD_SET(tcp_listener, &master);
	FD_SET(udp_listener, &master);

	fdmax = (tcp_listener > udp_listener ? tcp_listener : udp_listener) + 1;

	printf("listening for connections...\n");


	// TICK CONFIGURATION =================================
	// configure server tick
	timer_t timer_id;
	struct sigevent sev = {0};
	struct itimerspec its;

	TickState tick_state = { .ball_position = &ballPosition, .player_positions = player_positions, .udp_sock_fd = udp_listener, .tcp_sock_fd = tcp_listener, .clients = clients, .game_active = false, .left_score = 0, .right_score = 0};
	clock_gettime(CLOCK_MONOTONIC, &tick_state.latest_tick);

	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = tick;
	sev.sigev_value.sival_ptr = &tick_state;

	if (timer_create(CLOCK_MONOTONIC, &sev, &timer_id) == -1) {
		perror("timer_create");
		exit(1);
	}

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = TICK_RATE * 1000000;
	its.it_interval = its.it_value;

	if (timer_settime(timer_id, 0, &its, NULL) == -1) {
		perror("timer_settime");
		exit(1);
	}

	printf("Timer has started.\n");

	// MAIN LOOP ======================================
	for (;;) {
		read_fds = master;
		// check file descriptors in teh read_fds set and determine if any are ready for reading, writing, or have raised an exception
		// select modifies read_fds, only keeping fds that are ready for reading or writing in the set
		if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}

		// run through connections looking for data to read
		for (i = 0; i <= fdmax; i++) {
			// check if i is in read_fds
			if (FD_ISSET(i, &read_fds)) {
				if (i == tcp_listener) {
					// this means we have a new connection
					addrlen = sizeof remoteaddr;
					// accept connection to listener as new file descriptor
					newfd = accept(tcp_listener,
							(struct sockaddr *)&remoteaddr,
							&addrlen);

					if (newfd == -1) {
						perror("accept");
					} else {
						FD_SET(newfd, &master);  // add to master set
						if (newfd > fdmax) {
							fdmax = newfd;
						}

						printf("server:  new TCP connection from %s on "
								"socket %d\n",
								inet_ntop(remoteaddr.ss_family,
									get_in_addr((struct sockaddr*)&remoteaddr),
									remoteIP, INET6_ADDRSTRLEN),
								newfd);

					}
				} else if (i == udp_listener) {
					// handle data from UDP socket
					struct sockaddr_in from;
					socklen_t fromlen = sizeof(from);

					uint8_t buffer[1024];
					if ((nbytes = recvfrom(udp_listener, &buffer, sizeof(buffer), 0,
							(struct sockaddr *)&from, &fromlen)) <= 0) {
						// got error
						perror("recvfrom");
					}

					struct PositionMessage positionMessage;
					deserialize_position_message(buffer, &positionMessage);

					int client_index = positionMessage.id - 1;
					if (client_index >= 0 && client_index < MAX_CLIENTS && clients[client_index].active) {
						printf("Received UDP data from client %d (player_id %u)\n", client_index, positionMessage.id);
						// learn/refresh the client's real UDP address
						clients[client_index].addr = from;
						player_positions[client_index] = positionMessage.position;
						printf("setting position of client %d to (%f, %f)\n", client_index, positionMessage.position.x, positionMessage.position.y);
					} else {
						printf("Ignoring UDP packet with unknown player_id %u\n", positionMessage.id);
					}

					printf("udp_listener: got packet from %s\n",
						inet_ntoa(from.sin_addr));
					printf("udp_listener: packet is %d bytes long\n", nbytes);

				} else {
					// handle data from a tcp client
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							printf("server: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						close(i);
						FD_CLR(i, &master);	// remove from master set
					} else {
						// check opcode
						struct TcpMessage tcpMessage;
						deserialize_tcp_message(buf, &tcpMessage);
						if (tcpMessage.opcode == 0) {
							// register request
							printf("Registering player\n");

							// find a free slot
							int client_id = -1;
							for (int k = 0; k < MAX_CLIENTS; k++) {
								if (!clients[k].active) {
									clients[k].active = true;
									clients[k].player_id = k + 1;
									clients[k].tcp_fd = i;
									memset(&clients[k].addr, 0, sizeof(clients[k].addr));
									client_id = k + 1;
									printf("Registered client %d (player_id %d)\n", k, client_id);
									break;
								}
							}

							if (client_id == -1) {
								printf("No free client slots available\n");
							}
							// respond
							struct TcpResponse tcpResponse;
							tcpResponse.statuscode = 0;

							// send server config to client (big-endian / network byte order)
							uint32_t net_id = htonl(client_id);
							uint32_t rows = htonl(ROWS);
							uint32_t cols = htonl(COLS);
							
							float player_move_speed_f = PLAYER_MOVE_SPEED;
							uint32_t player_move_speed;
							memcpy(&player_move_speed, &player_move_speed_f, sizeof(player_move_speed));
							player_move_speed = htonl(player_move_speed);

							float ball_radius_f = BALL_RADIUS;
							uint32_t ball_radius;
							memcpy(&ball_radius, &ball_radius_f, sizeof(ball_radius));
							ball_radius = htonl(ball_radius);

							float player_length_f = PLAYER_LENGTH;
							uint32_t player_length;
							memcpy(&player_length, &player_length_f, sizeof(player_length));
							player_length = htonl(player_length);

							int offset = 0;
							memcpy(tcpResponse.msg + offset, &net_id, sizeof(net_id)); offset += sizeof(net_id);
							memcpy(tcpResponse.msg + offset, &rows, sizeof(rows)); offset += sizeof(rows);
							memcpy(tcpResponse.msg + offset, &cols, sizeof(cols)); offset += sizeof(cols);
							memcpy(tcpResponse.msg + offset, &player_move_speed, sizeof(player_move_speed)); offset += sizeof(player_move_speed);
							memcpy(tcpResponse.msg + offset, &ball_radius, sizeof(ball_radius)); offset += sizeof(ball_radius);
							memcpy(tcpResponse.msg + offset, &player_length, sizeof(player_length)); offset += sizeof(player_length);

							// clear rest of buffer
							memset(tcpResponse.msg + offset, 0, sizeof(tcpResponse.msg) - offset);

							uint8_t response_buffer[260];
							serialize_tcp_response(&tcpResponse, response_buffer);

							if (send(i, response_buffer, sizeof(response_buffer), 0) == -1) {
								perror("send");
							}

							printf("sent %lu bytes\n", sizeof(response_buffer));

						}
					}
				}
			}
		}
	}
}
