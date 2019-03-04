#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define ADDRESS "10.33.12.57"
#define PORT 42900

int
main()
{
	int                ret = 0;
	int                conn_fd;
	struct sockaddr_in server_addr = { 0 };

	server_addr.sin_family = AF_INET;

	server_addr.sin_port = htons(PORT);

	ret = inet_pton(AF_INET, ADDRESS, &server_addr.sin_addr);
	if (ret != 1) {
		if (ret == -1) {
			perror("inet_pton");
		}
		fprintf(stderr,
		        "failed to convert address %s "
		        "to binary net address\n",
		        ADDRESS);
		return -1;
	}

	fprintf(stdout, "CONNECTING: address=%s port=%d\n", ADDRESS, PORT);

	conn_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (conn_fd == -1) {
		perror("socket");
		return -1;
	}

	ret =
	  connect(conn_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if (ret == -1) {
		perror("connect");
		return -1;
	}

	return 0;
}
