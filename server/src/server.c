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

#define PORT "9034" // port we're listening on



struct PositionMessage {
	uint32_t id;
	struct Position {
		uint32_t x;
		uint32_t y;
	} position;
};

void positionMessageToString(const struct PositionMessage* msg, char* buffer, size_t buffer_size) {
	snprintf(buffer, buffer_size, "id: %d, x: %d, y: %d", 
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
	fd_set master;		// master file descriptor list
	fd_set read_fds;	// temp file descriptor list for select()
	int fdmax;		// largest file descriptor
	
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

	// listen on listener socket
	if (listen(tcp_listener, 10) == -1) {
		perror("listen");
		exit(3);
	}

	// add listener to master set
	FD_SET(tcp_listener, &master);
	FD_SET(udp_listener, &master);

	fdmax = (tcp_listener > udp_listener ? tcp_listener : udp_listener) + 1;

	printf("listening for connections...\n");

	//main loop
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

						printf("server:  new connection from %s on "
								"socket %d\n",
								inet_ntop(remoteaddr.ss_family,
									get_in_addr((struct sockaddr*)&remoteaddr),
									remoteIP, INET6_ADDRSTRLEN),
								newfd);
					}
				} else if (i == udp_listener) {
					// receive udp data
					struct PositionMessage positionMessage;
					if ((nbytes = recvfrom(udp_listener, &positionMessage, sizeof(positionMessage), 0,
							(struct sockaddr *)&remoteaddr, &addrlen)) <= 0) {
						// got error
						perror("recvfrom");
					}
					printf("udp_listener: got packet from %s\n",
						inet_ntop(remoteaddr.ss_family,
						get_in_addr((struct sockaddr *)&remoteaddr),
						remoteIP, sizeof remoteIP));
					printf("udp_listener: packet is %d bytes long\n", nbytes);
					buf[nbytes] = '\0';
					positionMessageToString(&positionMessage, buf, sizeof buf);
					printf("udp_listener: packet contains \"%s\"\n", buf);

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
