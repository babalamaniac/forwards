#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <linux/netfilter_ipv4.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include "fcntl.h"

void setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, NULL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void main() {
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        printf("socket listen failed\n");
    }
    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(12345);
    sockaddr.sin_addr.s_addr = inet_addr("192.168.107.2");
    int res = bind(socketfd, (struct sockaddr *)&sockaddr, sizeof(struct sockaddr));
    if (res == -1) {
        printf("socket bind failed\n");
    }
    listen(socketfd, 10);
    int clientSocket;
    while ((clientSocket = accept(socketfd, NULL, NULL)) > 0) {
        struct sockaddr_in destaddr;
        int n = sizeof(struct  sockaddr_in);
        getsockopt(clientSocket, SOL_IP, SO_ORIGINAL_DST, &destaddr, &n);
        printf("socket %d\n", clientSocket);
    }
}