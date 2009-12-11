#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

#define SERVER_IP "127.0.0.1"

int main(int argc, char *argv[]) {
	char msg[MESSAGESIZE];
	struct sockaddr_in raddr;
	int s;
	
	if(argc != 1) {
		printf("Usage: %s\n", argv[0]);
		return 1;
	}
	
	if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket creation failed");
		return 2;
	}
	
	raddr.sin_family = AF_INET;
	raddr.sin_port = htons(PORT);
	raddr.sin_addr.s_addr = inet_addr(SERVER_IP);
	memset(&(raddr.sin_zero), '\0', 8);
	
	if(connect(s, (struct sockaddr *)&raddr, sizeof(struct sockaddr)) == -1) {
		perror("Connection failed");
		return 3;
	}
	
	recv(s, msg, MESSAGESIZE, 0);
	printf("%s", msg);
	send(s, "EXI No thanks\n", strlen("EXI No thanks\n"), 0);

	return 0;
}
