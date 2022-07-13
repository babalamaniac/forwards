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

#include "proxycontext.c"

int sockaddr_size = sizeof(struct sockaddr_in);

// remote server address
struct sockaddr_in proxy_server_address;

void proxySend(struct event_context * context) {
    proxy_send(context->data);
}

void proxyRead(struct event_context * context) {
    proxy_read(context->data);
}

void error_handler(struct event_context * context) {
    close_proxy(context->data);
}

void init_remote_proxy(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;
    ssize_t send_size = proxy_context->address_read_size;
    send_size += write(proxy_context->dst_fd, &proxy_context->address + send_size, sockaddr_size - send_size);
    proxy_context->address_read_size = send_size;
    if (send_size == sockaddr_size) {
        // proxy inited, set all handler
        proxy_context->dst_context->handle_in = proxyRead;
        proxy_context->dst_context->handle_out = proxySend;
        proxy_context->src_context->handle_in = proxySend;
        proxy_context->src_context->handle_out = proxyRead;
        proxy_send(proxy_context);
        proxy_read(proxy_context);
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
        proxy_context->src_fd = src_fd;
        proxy_context->dst_fd = proxy_socket;
        proxy_context->address_read_size = 0;
        getsockopt(src_fd, SOL_IP, SO_ORIGINAL_DST, &(proxy_context->address), (socklen_t*)&sockaddr_size);


        // dst context. When init out handler with init_remote_proxy, to send origin address to the proxy
        struct event_context *dst_event_context = initContext();
        dst_event_context->data = proxy_context;
        dst_event_context->fd = proxy_socket;
        dst_event_context->handle_out = init_remote_proxy;
        dst_event_context->handle_err = error_handler;
        proxy_context->dst_context = dst_event_context;
        // src context. Ignore all events expect err until proxy socket inited.
        struct event_context *src_event_context = initContext();
        src_event_context->data = proxy_context;
        src_event_context->fd = src_fd;
        src_event_context->handle_err = error_handler;
        proxy_context->src_context = src_event_context;

        // add to epoll
        eventLoopAdd(eventLoop, src_event_context);
        eventLoopAdd(eventLoop, dst_event_context);

        // connect remote proxy async
        socketConnect(proxy_socket, proxy_server_address);
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

    printf("event loop start\n");
    fflush(stdout);
    mainLoop(eventLoop);
}