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

struct proxy_context {
    int src_fd;
    int dst_fd;
    int address_read_size;
    struct sockaddr_in address;
    struct event_context * src_context;
    struct event_context * dst_context;
};

void proxy_send(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;
    char buf[1024];
    ssize_t count = read(proxy_context->src_fd, buf, 1024);
    write(proxy_context->dst_fd, buf, count);
}

void proxy_recv(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;
    char buf[1024];
    ssize_t count = read(proxy_context->dst_fd, buf, 1024);
    write(proxy_context->src_fd, buf, count);
}

void error_handler(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    close(proxy_context->src_fd);
    close(proxy_context->dst_fd);
    eventLoopDel(context->eventLoop, context);
}

/**
 * origin address connect success, switch all handler to send/recv
 *
 * @param context
 */
void proxy_connect_success(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    proxy_context->src_context->handle_in = proxy_send;
    proxy_context->src_context->handle_out = proxy_recv;
    context->handle_in = proxy_recv;
    context->handle_out = proxy_send;
}

void init_proxy_connect(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    int address_size = sizeof (struct sockaddr_in);
    int read_size = proxy_context->address_read_size;
    read_size += read(context->fd, &(proxy_context->address), address_size - read_size);
    proxy_context->address_read_size = read_size;
    if (read_size == address_size) {
        // switch to null handler, wait for proxy socket connected
        context->handle_in = NULL;
        // connect remote proxy async
        socketConnect(proxy_context->dst_fd, proxy_context->address);
    }
}

void client_accept(struct event_context * context) {
    int eventLoop = context -> eventLoop;
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 20; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        // get original dest address
        setNonBlock(src_fd);

        int proxy_socket = createSocket();

        // build transfer context
        struct proxy_context *proxy_context = malloc(sizeof(struct proxy_context));
        proxy_context->address_read_size = 0;
        proxy_context->src_fd = src_fd;
        proxy_context->dst_fd = proxy_socket;

        // dst context
        struct event_context *dst_event_context = initContext();
        dst_event_context->data = proxy_context;
        dst_event_context->fd = proxy_socket;
        dst_event_context->handle_out = proxy_connect_success;
        dst_event_context->handle_in = proxy_connect_success;
        dst_event_context->handle_err = error_handler;
        // src context
        struct event_context *src_event_context = initContext();
        src_event_context->data = proxy_context;
        src_event_context->fd = src_fd;
        src_event_context->handle_in = init_proxy_connect;
        src_event_context->handle_err = error_handler;

        // add to epoll
        eventLoopAdd(eventLoop, src_event_context);
        eventLoopAdd(eventLoop, dst_event_context);
    }
}

int main(int num, char** args) {
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