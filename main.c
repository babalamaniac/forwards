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

#include "event_loop.c"
#include "sockets.c"

int sockaddr_size = sizeof(struct sockaddr_in);
int createServerSocket(const char * address, short port);
void setNonBlock(int fd);
struct sockaddr_in newAddress(const char * ip, short port);
int createSocket();
int socketConnect(int socketFD, struct sockaddr_in address);

// remote server address
struct sockaddr_in proxy_server_address;

struct transfer_context {
    int src_fd;
    int dst_fd;
};

void src_event_handler(struct event_context * context) {
    struct transfer_context * transfer_context = context->data;
    int size = sendfile(transfer_context->src_fd, transfer_context->dst_fd, 0, 1024);
    printf("%d\n", size);
}

void client_accept(struct event_context * context) {
    int eventLoop = context -> eventLoop;
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 20; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        // get original dest address
        setNonBlock(src_fd);
        struct sockaddr_in origin_address;
        getsockopt(src_fd, SOL_IP, SO_ORIGINAL_DST, &origin_address, &sockaddr_size);

        int proxy_socket = createSocket();

        // build transfer context
        struct transfer_context *transfer_context = malloc(sizeof(struct transfer_context));
        transfer_context->src_fd = src_fd;
        transfer_context->dst_fd = proxy_socket;

        // dst context
        struct event_context *dst_event_context = initContext();
        dst_event_context->data = transfer_context;
        dst_event_context->fd = proxy_socket;
        dst_event_context->handle_out = src_event_handler;
        // src context
        struct event_context *src_event_context = initContext();
        src_event_context->data = transfer_context;
        src_event_context->fd = src_fd;

        // add to epoll
        eventLoopAdd(eventLoop, src_event_context);
        eventLoopAdd(eventLoop, dst_event_context);

        // connect remote proxy async
        socketConnect(proxy_socket, proxy_server_address);

        // TODO send origin address
        write(proxy_socket, &origin_address, sizeof (struct sockaddr_in));
    }
}

int main(int num, char** args) {
    // init remote server address
    proxy_server_address = newAddress(args[1], atoi(args[2]));

    // init event loop
    int eventLoop = createEpollEventLoop();
    if (eventLoop <= 0) {
        exit(eventLoop);
    }

    // add server fd
    struct event_context server_fd_context;
    server_fd_context.fd = createServerSocket(args[3], atoi(args[4]));
    server_fd_context.handle_in = client_accept;
    eventLoopAdd(eventLoop, &server_fd_context);
    listen(server_fd_context.fd, 10);

    mainLoop(eventLoop);
}