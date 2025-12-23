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
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#define PORT "9034" // port we're listening on

const int COLS = 300;
const int ROWS = 50;

const int TICK_RATE = 16;

const float BALL_RADIUS = 2.0;
const float PLAYER_LENGTH = 2.0;

const int MAX_CLIENTS = 4;

typedef struct {
	struct sockaddr_in addr;
	uint32_t player_id;
	bool active;
} Client;

typedef struct {
	float x;
	float y;
	float dx;
	float dy;
} Position;

typedef struct {
	Position* ball_position;
	int udp_sock_fd;
	struct timespec latest_tick;
	Client* clients;
} TickState;

struct __attribute((packed)) PositionMessage {
	uint32_t id;
	Position position;
};

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

	printf("New ball position: (%f, %f)\n", tick_state->ball_position->x, tick_state->ball_position->y);

	struct PositionMessage msg;
	// htonl converts unint32_t to network-byte order (i.e., big-endian)
	msg.id = htonl(0);
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

	tick_state->latest_tick = now;
	
}

int find_or_add_udp_client(Client* clients, struct sockaddr_in *addr)
{
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (clients[i].active &&
			clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
			clients[i].addr.sin_port == addr->sin_port)
		return i;
	}

	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (!clients[i].active) {
			clients[i].addr = *addr;
			clients[i].active = true;
			clients[i].player_id = i;
			printf("Registered UDP client %d\n", i);
			return i;
		}
	}
	return -1;
}


void positionMessageToString(const struct PositionMessage* msg, char* buffer, size_t buffer_size) {
	snprintf(buffer, buffer_size, "id: %d, x: %f, y: %f", 
             msg->id, msg->position.x, msg->position.y);
}

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
	// INIT PHYSICS ================================
	Position ballPosition; 
	ballPosition.x = COLS / 2.0;
	ballPosition.y = ROWS / 2.0;
	ballPosition.dx = 5.0;
	ballPosition.dy = 5.0;


	// TCP NETWORKING ============================
	fd_set master;		// master file descriptor list
	fd_set read_fds;	// temp file descriptor list for select()
	int fdmax;		// largest file descriptor
	
	Client clients[MAX_CLIENTS];
	memset(clients, 0, sizeof(clients));	
	
	int tcp_listener, udp_listener;		// FD for the server listener
	int newfd;		// newly accepted fd
	struct sockaddr_storage remoteaddr;	//client address
	socklen_t addrlen;

	char buf[256];		// buffer for client data
	int nbytes;

	char remoteIP[INET6_ADDRSTRLEN];

	int yes=1;
	int i, j, rv;

	struct addrinfo hints, *ai, *p;

	FD_ZERO(&master);	// clear the master and temp sets
	FD_ZERO(&read_fds);

	// get a socket and bind it
	// the server will listen on this socket for connections and data
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
	
	TickState tick_state = { .ball_position = &ballPosition, .udp_sock_fd = udp_listener, .clients = clients};
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
					// when we get a packet over the UDP socket, if we don't have it already add it to our list of clients
					struct sockaddr_in from;
					socklen_t fromlen = sizeof(from);
					
					struct PositionMessage positionMessage;
					if ((nbytes = recvfrom(udp_listener, &positionMessage, sizeof(positionMessage), 0,
							(struct sockaddr *)&from, &fromlen)) <= 0) {
						// got error
						perror("recvfrom");
					}

					find_or_add_udp_client(clients, &from);

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
						// we got some data from the client
						for (j = 0; j <= fdmax; j++) {
							// send to everyone (i.e., every fd in the master set)!
							if (FD_ISSET(j, &master)) {
								// except the listener and the sender
								if (j != tcp_listener && j != udp_listener && j != i) {
									if (send(j, buf, nbytes, 0) == -1) {
										perror("send");
									}
								}
							}
						}
					}
				}
			}
		}
	}
}
