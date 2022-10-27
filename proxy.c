#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>

#include "proxy_context.c"

int address_size = sizeof(struct sockaddr_in);

void init_proxy_connect(struct event_context * src_context) {
    struct proxy_init_context * proxy_init_context = get_ext(src_context);

    ssize_t read_size = proxy_init_context->size;
    read_size += read(src_context->fd, &(proxy_init_context->address) + read_size, address_size - read_size);
    if (read_size == proxy_init_context->size) {
        printf("init proxy read size = 0\n");
        return;
    }
    proxy_init_context->size = read_size;
    if (read_size == address_size) {
        printf("origin address received, src_fd=%d\n", src_context->fd);
        struct event_context * dst_context = event_connect(src_context -> eventLoop, &proxy_init_context->address, PROXY_CONTEXT_SIZE);
        init_proxy_event_context(src_context);
        init_proxy_event_context(dst_context);
        bind_context(src_context, dst_context);
    }
}

void init_child(struct event_context * context) {
    context -> handle_in = init_proxy_connect;

    struct proxy_init_context * proxy_init_context = get_ext(context);
    proxy_init_context -> src_event_context = context;
    proxy_init_context -> size = 0;
}

int main(int num, char** args) {
    // start event loop
    start_event_loop(createServerSocket(args[1], atoi(args[2])), init_child, PROXY_CONTEXT_SIZE);
}