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

int createSocket() {
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    int flags = fcntl(socketFD, F_GETFL, NULL);
    fcntl(socketFD, F_SETFL, flags | O_NONBLOCK);
    return socketFD;
}

struct sockaddr_in newAddress(const char * ip, short port) {
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(ip);
    return address;
}

int socketConnect(int socketFD, struct sockaddr_in address) {
    int result = connect(socketFD, (struct sockaddr *)&address, sizeof(struct sockaddr));
    return result;
}

int createServerSocket(const char * ip, short port) {
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        printf("socket listen failed\n");
        return -1;
    }
    setNonBlock(socketfd);
    struct sockaddr_in address = newAddress(ip, port);
    int res = bind(socketfd, (struct sockaddr *)&address, sizeof(struct sockaddr));
    if (res == -1) {
        printf("socket bind failed\n");
        return -1;
    }
    return socketfd;
}

ssize_t transfer(int socketA, int socketB) {
    char buf[1024];
    // sendfile not support socket to socket
    ssize_t count = read(socketA, buf, 1024);
    write(socketB, buf, count);
    return count;
}

int createClientSocket(const char * address, short port) {
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD == -1) {
        printf("socket connect failed\n");
        return -1;
    }
    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = inet_addr(address);
    setNonBlock(socketFD);
    int result = connect(socketFD, (struct sockaddr *)&address, sizeof(struct sockaddr));
    if (result == -1) {
        printf("%s\n", strerror(errno));
        close(socketFD);
        return -1;
    }
    return socketFD;
}