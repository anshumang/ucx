#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 10
#define PORT 38593

/**
 * Encapsulates the properties of the server.
 */
typedef struct server {
	// file descriptor of the socket in passive
	// mode to wait for connections.
	int listen_fd;
} server_t;

/**
 * Creates a socket for the server and makes it passive such that
 * we can wait for connections on it later.
 */
int server_listen(server_t* server);
int
server_listen(server_t* server)
{
	int                err         = 0;
	struct sockaddr_in server_addr = { 0 };

	err = (server->listen_fd = socket(AF_INET, SOCK_STREAM, 0));
	if (err == -1) {
		perror("socket");
		printf("Failed to create socket endpoint\n");
		return err;
	}

	// `sockaddr_in` provides ways of representing a full address
	// composed of an IP address and a port.
	//
	// SIN_FAMILY   address family   AF_INET refers to the address
	//                               family related to internet
	//                               addresses
	//
	// S_ADDR       address (ip) in network byte order (big endian)
	// SIN_PORT     port in network byte order (big endian)
	server_addr.sin_family = AF_INET;

        // INADDR_ANY is a special constant that signalizes "ANY IFACE",
        // i.e., 0.0.0.0.
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PORT);

	// bind() assigns the address specified to the socket referred 
        // to by the file descriptor (`listen_fd`).
        //
        // Here we cast `sockaddr_in` to `sockaddr` and specify the
        // length such that `bind` can pick the values from the
        // right offsets when interpreting the structure pointed to.
	err = bind(server->listen_fd,
	           (struct sockaddr*)&server_addr,
	           sizeof(server_addr));
	if (err == -1) {
		perror("bind");
		printf("Failed to bind socket to address\n");
		return err;
	}

	// listen() marks the socket referred to by sockfd as a passive socket,
	// that is, as a socket that will be used to accept incoming connection
	// requests using accept(2).
	err = listen(server->listen_fd, BACKLOG);
	if (err == -1) {
		perror("listen");
		printf("Failed to put socket in passive mode\n");
		return err;
	}
        //fprintf(stderr, "listen on %s:%p\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
}
/**
 * Accepts new connections and then prints `Hello World` to
 * them.
 */
int
server_accept(server_t* server)
{
	int err = 0;
	int conn_fd;
	socklen_t client_len;
	struct sockaddr_in client_addr;

	client_len = sizeof(client_addr);

	err =
	  (conn_fd = accept(
	     server->listen_fd, (struct sockaddr*)&client_addr, &client_len));
	if (err == -1) {
		perror("accept");
		printf("failed accepting connection\n");
		return err;
	}

	printf("Client connected!\n");

	err = close(conn_fd);
	if (err == -1) {
		perror("close");
		printf("failed to close connection\n");
		return err;
	}

	return err;
}

int
main()
{
	int                err = 0;
	int                conn_fd;
	server_t server = { 0 };
	err = server_listen(&server);
	if (err) {
		printf("Failed to listen on address 0.0.0.0:%d\n", 
                        PORT);
		return err;
	}

	for (;;) {
		err = server_accept(&server);
		if (err) {
			printf("Failed accepting connection\n");
			return err;
		}
	}

	return 0;
}
