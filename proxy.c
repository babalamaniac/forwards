#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sockets.c"
#include "proxycontext.c"

int address_size = sizeof(struct sockaddr_in);

void proxySend(struct event_context * context) {
    proxy_send(context->data);
}

void proxyRead(struct event_context * context) {
    proxy_read(context->data);
}

void error_handler(struct event_context * context) {
    close_proxy(context->data);
}

/**
 * origin address connect success, switch all handler to send/recv
 *
 * @param context
 */
void proxy_connect_success(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    proxy_context->src_context->handle_in = proxySend;
    proxy_context->src_context->handle_out = proxyRead;
    context->handle_in = proxyRead;
    context->handle_out = proxySend;
}

void init_proxy_connect(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    int read_size = proxy_context->address_read_size;
    read_size += read(context->fd, &(proxy_context->address) + read_size, address_size - read_size);
    if (read_size == proxy_context->address_read_size) {
        close_proxy(proxy_context);
        return;
    }
    proxy_context->address_read_size = read_size;
    if (read_size == address_size) {
        // switch to null handler, wait for proxy socket connected
        context->handle_in = NULL;
        // connect remote proxy async
        proxy_context->dst_context->handle_in = proxy_connect_success;
        proxy_context->dst_context->handle_out = proxy_connect_success;
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
        dst_event_context->handle_err = error_handler;
        proxy_context->dst_context = dst_event_context;
        // src context
        struct event_context *src_event_context = initContext();
        src_event_context->data = proxy_context;
        src_event_context->fd = src_fd;
        src_event_context->handle_in = init_proxy_connect;
        src_event_context->handle_err = error_handler;
        proxy_context->src_context = src_event_context;

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
    server_fd_context.fd = createServerSocket(args[1], atoi(args[2]));
    server_fd_context.handle_in = client_accept;
    eventLoopAdd(eventLoop, &server_fd_context);
    listen(server_fd_context.fd, 10);

    mainLoop(eventLoop);
}