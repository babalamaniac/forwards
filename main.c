#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netfilter_ipv4.h>

#include "base_proxy.c"

int address_size = sizeof(struct sockaddr_in);

// remote server address
struct sockaddr_in proxy_server_address;

void init_remote_proxy(struct event_context * context) {
    struct proxy_init_context * init_context = context->data;
    ssize_t send_size = init_context -> size;
    send_size += write(context -> fd, &(init_context->address) + send_size, address_size - send_size);
    init_context -> size = send_size;
    if (send_size == address_size) {
        // proxy inited, set all handler
        struct proxy_context *src = init_proxy_context(init_context ->src_fd);
        struct proxy_context *dst = init_proxy_context(init_context ->dst_fd);
        bind_context(src, dst);

        init_proxy_event_context(context, dst);

        eventLoopAdd(context->eventLoop, init_proxy_event_context(initContext(), src));
        free(init_context);
    }
}

void client_accept(struct event_context * context) {
    int eventLoop = context -> eventLoop;
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 20; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        // get original dest address
        setNonBlock(src_fd);
        printf("accept src fd=%d\n", src_fd);

        int proxy_socket = createSocket();

        struct proxy_init_context * init_context = init_proxy_init_context(src_fd, proxy_socket);
        getsockopt(src_fd, SOL_IP, SO_ORIGINAL_DST, &(init_context->address), (socklen_t*)&address_size);

        struct event_context * event_context = initContext();
        event_context->data = init_context;
        event_context->fd = proxy_socket;
        event_context->handle_out = init_remote_proxy;
        event_context->handle_err = handle_close;
        event_context->handle_close = handle_close;

        // add to epoll
        eventLoopAdd(eventLoop, event_context);

        // connect remote proxy async
        socketConnect(proxy_socket, proxy_server_address);
    }
}

int main(int num, char** args) {
    // init remote server address
    proxy_server_address = newAddress(args[1], atoi(args[2]));
    // start event loop
    start_event_loop(createServerSocket(args[3], atoi(args[4])), client_accept);
}
void handle_out_a(struct event_context * context) {
    shutdown(context -> fd, SHUT_WR);
}
void handle_in_a(struct event_context * context) {
//    char buf[1];
//    read(context -> fd, buf, 1);
//    printf("%c\n", *buf);
//    fflush(stdout);
}
int main_2(int num, char** args) {
    int epoll_fd = epoll_create(1);
    int socket_fd = createSocket();

    struct event_context * event_context = initContext();
    event_context->fd = socket_fd;

    struct epoll_event * epoll_event = malloc(sizeof(struct epoll_event));
    struct event_context * eventContext = initContext();
    eventContext -> fd = socket_fd;
    eventContext -> handle_out = handle_out_a;
    eventContext -> handle_in = handle_in_a;
    epoll_event -> data.ptr = eventContext;
    epoll_event -> events = EPOLLET | EPOLLOUT | EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
    int result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, epoll_event);

    socketConnect(socket_fd, newAddress("192.168.107.2", 20012));
//    shutdown(socket_fd, SHUT_WR);
    mainLoop(epoll_fd);
}


