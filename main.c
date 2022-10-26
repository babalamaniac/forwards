#define _GNU_SOURCE

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netfilter_ipv4.h>

#include "proxy_context.c"

int address_size = sizeof(struct sockaddr_in);

// remote server address
struct sockaddr_in proxy_server_address;

void init_remote_proxy(struct event_context * dst_context) {
    struct proxy_init_context * init_context = get_ext(dst_context);
    ssize_t send_size = init_context -> size;
    send_size += write(dst_context -> fd, &(init_context->address) + send_size, address_size - send_size);
    init_context -> size = send_size;
    if (send_size == address_size) {
        struct event_context * src_context = init_context -> src_event_context;
        init_proxy_event_context(src_context);
        init_proxy_event_context(dst_context);
        bind_context(src_context, dst_context);

        proxy_handle_in(src_context);
    }
}

void init_child(struct event_context * context) {
    int fd = context -> fd;
    struct proxy_init_context * init_context = init_proxy_init_context(fd, 0);
    getsockopt(context -> fd, SOL_IP, SO_ORIGINAL_DST, &(init_context->address), (socklen_t*)&address_size);
    init_context -> src_event_context = context;

    struct init_context client_init_context = {
            init_remote_proxy,
            PROXY_CONTEXT_SIZE,
            init_context
    };
    event_connect(context -> eventLoop, &proxy_server_address, client_init_context);
}

int main(int num, char** args) {
    // init remote server address
    proxy_server_address = newAddress(args[1], atoi(args[2]));
    // start event loop
    start_event_loop(createServerSocket(args[3], atoi(args[4])), init_child, sizeof(struct proxy_context));
}
