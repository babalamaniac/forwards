#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
    printf("proxy establish, src_fd=%d, dst_fd=%d\n", proxy_context->src_fd, proxy_context->dst_fd);

    proxy_context->src_context->handle_in = proxySend;
    proxy_context->src_context->handle_out = proxyRead;
    proxy_context->dst_context->handle_in = proxyRead;
    proxy_context->dst_context->handle_out = proxySend;

    proxyRead(context);
    proxySend(context);
}

void init_proxy_connect(struct event_context * context) {
    struct proxy_context * proxy_context = context->data;

    ssize_t read_size = proxy_context->address_read_size;
    read_size += read(context->fd, &(proxy_context->address) + read_size, address_size - read_size);
    if (read_size == proxy_context->address_read_size) {
        close_proxy(proxy_context);
        return;
    }
    proxy_context->address_read_size = read_size;
    if (read_size == address_size) {
        printf("origin address received, src_fd=%d\n", context->fd);
        // switch to null handler, wait for proxy socket connected
        context->handle_in = NULL;
        // connect remote proxy async
        // build transfer context
        int proxy_socket = createSocket();
        setNonBlock(proxy_socket);

        // dst context
        struct event_context *event_context = initContext();
        event_context->data = proxy_context;
        event_context->fd = proxy_socket;
        event_context->handle_in = proxy_connect_success;
        event_context->handle_out = proxy_connect_success;
        event_context->handle_err = error_handler;
        proxy_context->dst_context = event_context;
        proxy_context->dst_fd = proxy_socket;
        eventLoopAdd(context->eventLoop, event_context);
        socketConnect(proxy_context->dst_fd, proxy_context->address);
    }
}

void client_accept(struct event_context * context) {
    int eventLoop = context -> eventLoop;
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 20; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        printf("client accept, src_fd=%d\n", src_fd);
        // get original dest address
        setNonBlock(src_fd);

        struct proxy_context *proxy_context = malloc(sizeof(struct proxy_context));
        proxy_context->address_read_size = 0;
        proxy_context->src_fd = src_fd;
        proxy_context->eventLoop = eventLoop;

        // src context
        struct event_context *event_context = initContext();
        event_context->data = proxy_context;
        event_context->fd = src_fd;
        event_context->handle_in = init_proxy_connect;
        event_context->handle_err = error_handler;
        proxy_context->src_context = event_context;
        proxy_context->src_fd = src_fd;

        // add to epoll
        eventLoopAdd(eventLoop, event_context);
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

    printf("event loop start\n");
    mainLoop(eventLoop);
}