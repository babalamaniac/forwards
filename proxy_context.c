#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "event_loop.c"

#define READ_CLOSED (1 << 0)
#define WRITE_CLOSED (1 << 1)

struct proxy_context {
    struct event_context * peer_context;
    int out_buf;
    int in_buf;
    int pipe[2];
    int state;
};

// copy between fd
ssize_t copy(int from, int to, int type) {
    int size;
    int buf = from;
    if (ioctl(buf, FIONREAD, &size) == -1) {
        printf("get readable size error, errno=%d, errmsg=%s\n", errno, strerror(errno));
        size = 0;
    }
    ssize_t count;
    if (type) {
        int arr[size + 1];
        count = read(buf, arr, size + 1);
        if (count <= 0) {
            return count;
        }
        for (int i = 0; i < count; i++) arr[i] = ~(arr[i]);
        count = write(to, arr, count);
    } else {
        count = splice(buf, 0, to, 0, size + 1, SPLICE_F_NONBLOCK);
    }
    return count;
}

void proxy_handle_out(struct event_context * event_context) {
    struct proxy_context * proxy_context = get_ext(event_context);
    if (proxy_context -> state & WRITE_CLOSED) {
        printf("write channel closed, dst fd=%d src fd=%d\n", proxy_context -> peer_context -> fd, event_context -> fd);
        return;
    }
    ssize_t count = copy(proxy_context -> out_buf, event_context -> fd, 0);
    if (count == 0) {
        proxy_context -> state |= WRITE_CLOSED;
        shutdown(event_context -> fd, SHUT_WR);
    }
}

void proxy_handle_in(struct event_context * event_context) {
    struct proxy_context * proxy_context = get_ext(event_context);
    if (proxy_context -> state & READ_CLOSED) {
        printf("read channel closed, dst fd=%d src fd=%d\n", event_context -> fd, proxy_context -> peer_context -> fd);
        return;
    }
    copy(event_context -> fd, proxy_context -> in_buf, 1);
    proxy_handle_out(proxy_context -> peer_context);
}

void do_read_close(struct event_context * event_context) {
    struct proxy_context * context = get_ext(event_context);
    if (context -> state & READ_CLOSED) {
        return;
    }
    context -> state |= READ_CLOSED;
    close(context -> in_buf);
    proxy_handle_out(context -> peer_context);
}

void do_close(struct event_context * event_context) {
    struct proxy_context * context = get_ext(event_context);
    do_read_close(event_context);
    if (context -> state & WRITE_CLOSED) {
        return;
    }
    context -> state |= WRITE_CLOSED;
    close(context -> out_buf);
}

struct proxy_context * init_proxy_context() {
    struct proxy_context *proxy_context = malloc(sizeof(struct proxy_context));
    proxy_context -> state = 0;
    proxy_context -> peer_context = NULL;
    if (pipe2(proxy_context -> pipe, O_NONBLOCK) == -1) {
        printf("create pipe error, errno=%d, errmsg=%s\n", errno, strerror(errno));
    }
    printf("create pipe, 0=%d, 1=%d\n", proxy_context -> pipe[0], proxy_context -> pipe[1]);
    proxy_context -> in_buf = (proxy_context -> pipe)[1];
    fcntl((proxy_context -> pipe)[1], F_SETPIPE_SZ, 100 * 1024 * 1024);
    return proxy_context;
}

void bind_context(struct event_context * a, struct event_context * b) {
    struct proxy_context * proxy_context_a = get_ext(a);
    struct proxy_context * proxy_context_b = get_ext(b);
    proxy_context_a -> peer_context = b;
    proxy_context_b -> peer_context = a;
    proxy_context_a -> out_buf = proxy_context_b -> pipe[0];
    proxy_context_b -> out_buf = proxy_context_a -> pipe[0];
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
    if (pipe2(proxy_context -> pipe, O_NONBLOCK) == -1) {
        printf("create pipe error, errno=%d, errmsg=%s\n", errno, strerror(errno));
    }
    printf("create pipe, 0=%d, 1=%d\n", proxy_context -> pipe[0], proxy_context -> pipe[1]);
    proxy_context -> in_buf = (proxy_context -> pipe)[1];
    fcntl((proxy_context -> pipe)[1], F_SETPIPE_SZ, 100 * 1024 * 1024);
    return proxy_context;
}

int max(int a, int b) {
    return a > b ? a : b;
}

#define PROXY_CONTEXT_SIZE max(sizeof (struct proxy_context), sizeof (struct proxy_init_context))