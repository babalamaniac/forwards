#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "event_loop.c"
#include "sockets.c"

#define READ_CLOSED (1 << 0)
#define WRITE_CLOSED (1 << 1)

struct proxy_context {
    int fd;
    struct proxy_context * peer;
    int peer_buf;
    int pipe[2];
    int state;
};

// copy between fd
ssize_t copy(int from, int to) {
    int size;
    int buf = from;
    if (ioctl(buf, FIONREAD, &size) == -1) {
        printf("get readable size error, errno=%d, errmsg=%s\n", errno, strerror(errno));
        size = 0;
    }
    ssize_t count = splice(buf, 0, to, 0, size + 1, SPLICE_F_NONBLOCK);
    return count;
}

// copy 'from''s buffered data to 'to'
ssize_t transfer(struct proxy_context * from, struct proxy_context * to) {
    return copy(from -> pipe[0], to -> fd);
}

// copy from socket buffer to pipe buffer
ssize_t mv_into_buf(struct proxy_context * context) {
    return copy(context -> fd, context -> pipe[1]);
}

int proxy_need_close(struct proxy_context * proxy_context) {
    return (proxy_context -> state & READ_CLOSED) && (proxy_context -> state & WRITE_CLOSED);
}

void proxy_handle_out(struct proxy_context * proxy_context) {
    if (proxy_context -> state & WRITE_CLOSED) {
        printf("write channel closed, dst fd=%d src fd=%d\n", proxy_context -> peer -> fd, proxy_context -> fd);
        return;
    }
    ssize_t count = transfer(proxy_context -> peer, proxy_context);
    if (count == 0) {
        proxy_context -> state |= WRITE_CLOSED;
        shutdown(proxy_context -> fd, SHUT_WR);
    }
}

void proxy_handle_in(struct proxy_context * proxy_context) {
    if (proxy_context -> state & READ_CLOSED) {
        printf("read channel closed, dst fd=%d src fd=%d\n", proxy_context -> fd, proxy_context -> peer -> fd);
        return;
    }
    mv_into_buf(proxy_context);
    transfer(proxy_context, proxy_context -> peer);
}

void do_read_close(struct proxy_context * context) {
    if (context -> state & READ_CLOSED) {
        return;
    }
    context -> state |= READ_CLOSED;
    close(context -> pipe[1]);
    proxy_handle_out(context -> peer);
}

void do_close(struct proxy_context * context) {
    do_read_close(context);
    context -> state |= WRITE_CLOSED;
    if (proxy_need_close(context -> peer)) {
        free(context -> peer);
        free(context);
    }
}

struct proxy_context * init_proxy_context(int fd) {
    struct proxy_context *proxy_context = malloc(sizeof(struct proxy_context));
    proxy_context -> state = 0;
    proxy_context -> fd = fd;
    proxy_context -> peer = NULL;
    if (pipe2(proxy_context -> pipe, O_NONBLOCK) == -1) {
        printf("create pipe error, errno=%d, errmsg=%s\n", errno, strerror(errno));
    }
    printf("create pipe, 0=%d, 1=%d\n", proxy_context -> pipe[0], proxy_context -> pipe[1]);
    fcntl((proxy_context -> pipe)[1], F_SETPIPE_SZ, 100 * 1024 * 1024);
    return proxy_context;
}

void bind_context(struct proxy_context * a, struct proxy_context * b) {
    a -> peer = b;
    b -> peer = a;
}

void bind_context_a(struct event_context * a, struct event_context * b) {
    ((struct proxy_context *)(a + 1)) -> peer = (struct proxy_context *)(b + 1);
    ((struct proxy_context *)(b + 1)) -> peer = (struct proxy_context *)(a + 1);
}

struct proxy_init_context {
    int src_fd;
    int dst_fd;
    ssize_t size;
    struct sockaddr_in address;
};

struct proxy_init_context * init_proxy_init_context(int src_fd, int dst_fd) {
    struct proxy_init_context * context = malloc(sizeof(struct proxy_init_context));
    context -> src_fd = src_fd;
    context -> dst_fd = dst_fd;
    context -> size = 0;
    return context;
}
