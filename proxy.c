#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy_context.c"

int address_size = sizeof(struct sockaddr_in);

void init_proxy_connect(struct event_context * src_context) {
    struct proxy_init_context * proxy_init_context = src_context->data;

    ssize_t read_size = proxy_init_context->size;
    read_size += read(src_context->fd, &(proxy_init_context->address) + read_size, address_size - read_size);
    if (read_size == proxy_init_context->size) {
        printf("init proxy read size = 0\n");
        return;
    }
    proxy_init_context->size = read_size;
    if (read_size == address_size) {
        printf("origin address received, src_fd=%d\n", src_context->fd);
        int proxy_socket = createSocket();
        struct event_context * dst_context = create_proxy_event_context(proxy_socket);
        init_proxy_event_context(src_context);
        init_proxy_event_context(dst_context);
        bind_context(src_context, dst_context);

        // dst context
        eventLoopAdd(src_context->eventLoop, dst_context);
        socketConnect(proxy_socket, proxy_init_context->address);

        free(proxy_init_context);
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

        struct proxy_init_context * proxy_init_context = init_proxy_init_context(src_fd, 0);
        proxy_init_context -> size = 0;

        // src context
        struct event_context *event_context = create_proxy_event_context(src_fd);
        event_context->data = proxy_init_context;
        event_context->handle_in = init_proxy_connect;

        // add to epoll
        eventLoopAdd(eventLoop, event_context);
    }
}

int main(int num, char** args) {
    // start event loop
    start_event_loop(createServerSocket(args[1], atoi(args[2])), client_accept);
}