#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "event_loop.c"

#define READ_CLOSED (1 << 0)
#define WRITE_CLOSED (1 << 1)

socklen_t optlen = sizeof (int);

struct proxy_context {
    struct event_context * peer_context;
    int state;
};

size_t move(int from, int to) {
    int send_buf_size, send_buf_used, unread_size, size, send_buf_remain;
    ioctl(to, TIOCOUTQ, &send_buf_used);
    getsockopt(to, SOL_SOCKET, SO_SNDBUF, &send_buf_size, &optlen);
    ioctl(from, FIONREAD, &unread_size);
    send_buf_remain = send_buf_size - send_buf_used;
    size = send_buf_remain < unread_size ? send_buf_remain : unread_size;
    if (size <= 0) {
        return -1;
    }
    printf("send_buf_size=%d, send_buf_used=%d, unread_size=%d, size=%d, send_buf_remain=%d\n", send_buf_size, send_buf_used, unread_size, size, send_buf_remain);

    unsigned char buf[size];
    ssize_t read_size = read(from, buf, size);
    for (int i = 0; i < read_size; ++i) buf[i] = ~buf[i];
    ssize_t write_size = write(to, buf, read_size);
    printf("expect size = %d, actual read_size = %ld, actual write size = %ld\n", size, read_size, write_size);
}

void proxy_handle_out(struct event_context * event_context) {
    struct proxy_context * proxy_context = get_ext(event_context);
    if (proxy_context -> state & WRITE_CLOSED) {
        printf("write channel closed, dst fd=%d src fd=%d\n", proxy_context -> peer_context -> fd, event_context -> fd);
        return;
    }
    move(proxy_context -> peer_context -> fd, event_context -> fd);
}

void proxy_handle_in(struct event_context * event_context) {
    struct proxy_context * proxy_context = get_ext(event_context);
    if (proxy_context -> state & READ_CLOSED) {
        printf("read channel closed, dst fd=%d src fd=%d\n", event_context -> fd, proxy_context -> peer_context -> fd);
        return;
    }
    move(event_context -> fd, proxy_context -> peer_context -> fd);
}

void do_read_close(struct event_context * event_context) {
    struct proxy_context * context = get_ext(event_context);
    if (context -> state & READ_CLOSED) {
        return;
    }
    context -> state |= READ_CLOSED;
    shutdown(context -> peer_context -> fd, SHUT_WR);
    proxy_handle_out(context -> peer_context);
}

void do_close(struct event_context * event_context) {
    struct proxy_context * context = get_ext(event_context);
    do_read_close(event_context);
    if (context -> state & WRITE_CLOSED) {
        return;
    }
    context -> state |= WRITE_CLOSED;
    shutdown(event_context -> fd, SHUT_WR);
}

struct proxy_context * init_proxy_context() {
    struct proxy_context *proxy_context = malloc(sizeof(struct proxy_context));
    proxy_context -> state = 0;
    proxy_context -> peer_context = NULL;
    return proxy_context;
}

void bind_context(struct event_context * a, struct event_context * b) {
    struct proxy_context * proxy_context_a = get_ext(a);
    struct proxy_context * proxy_context_b = get_ext(b);
    proxy_context_a -> peer_context = b;
    proxy_context_b -> peer_context = a;
}

struct proxy_init_context {
    int src_fd;
    int dst_fd;
    ssize_t size;
    struct event_context * src_event_context;
    struct sockaddr_in address;
};

struct proxy_init_context * init_proxy_init_context(int src_fd, int dst_fd) {
    struct proxy_init_context * context = malloc(sizeof(struct proxy_init_context));
    context -> src_fd = src_fd;
    context -> dst_fd = dst_fd;
    context -> size = 0;
    return context;
}

struct event_context * create_proxy_event_context(int fd) {
    struct event_context * event_context = initContext(sizeof(struct proxy_context));
    event_context -> fd = fd;
    return event_context;
}

struct proxy_context * init_proxy_event_context(struct event_context * event_context) {
    event_context -> handle_in = proxy_handle_in;
    event_context -> handle_out = proxy_handle_out;
    event_context -> handle_close = do_close;
    event_context -> handle_read_close = do_read_close;

    struct proxy_context * proxy_context = get_ext(event_context);
    proxy_context -> state = 0;
    proxy_context -> peer_context = NULL;
    return proxy_context;
}

int max(int a, int b) {
    return a > b ? a : b;
}

#define PROXY_CONTEXT_SIZE max(sizeof (struct proxy_context), sizeof (struct proxy_init_context))