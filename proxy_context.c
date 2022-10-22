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
    int pipe[2];
    int state;
};

ssize_t proxy(int src, int dst) {
    int size;
    if (ioctl(src, FIONREAD, &size) == -1) {
        printf("get readable size error, errno=%d, errmsg=%s\n", errno, strerror(errno));
        size = 0;
    }
    splice(src, 0, dst, 0, size, SPLICE_F_NONBLOCK);
    ssize_t count = splice(src, 0, dst, 0, 1, SPLICE_F_NONBLOCK);
    if (count == 0) {
	printf("end of src_fd=%d, dst_fd=%d\n", src, dst);
        fflush(stdout);
        return 0;
    }
    if (count == -1) {
        if (errno == EAGAIN) {
            return -1;
        } else {
            printf("proxy msg error, sendfile count -1, src=%d, dst=%d, errno=%d, error=%s\n", src, dst, errno, strerror(errno));
            fflush(stdout);
            return 0;
        }
    }

    return -1;
}

void do_proxy(struct proxy_context * from, struct proxy_context * to) {
    printf("from fd=%d  state=%d, to fd=%d state=%d\n", from -> fd, from->state, to->fd,to->state);
    if (to -> state & WRITE_CLOSED) {
        from -> state |= READ_CLOSED;
        return;
    }
    ssize_t result;
    if (!(from -> state & READ_CLOSED)) {
        result = proxy(from -> fd, from -> pipe[1]);
        if (result == 0) {
            from -> state |= READ_CLOSED;
            close(from -> pipe[1]);
            shutdown(from -> fd, SHUT_RD);
        }
    }
    result = proxy((from -> pipe)[0], to -> fd);
    if (result == 0) {
        to -> state |= WRITE_CLOSED;
        close(from -> pipe[0]);
        close(to -> fd);
    }
    printf("do proxy from fd=%d  state=%d, to fd=%d state=%d\n", from -> fd, from->state, to->fd,to->state);
}

void do_read(struct proxy_context * proxy_context) {
    proxy(proxy_context -> peer -> pipe[0], proxy_context -> fd);
}

void do_write(struct proxy_context * proxy_context) {
    proxy(proxy_context -> fd, proxy_context -> pipe[1]);
}

void proxy_handle_in(struct proxy_context * proxy_context) {
    do_proxy(proxy_context, proxy_context -> peer);
}

void proxy_handle_out(struct proxy_context * proxy_context) {
    do_proxy(proxy_context -> peer, proxy_context);
}

int proxy_need_close(struct proxy_context * proxy_context) {
    return (proxy_context -> state & READ_CLOSED) && (proxy_context -> state & WRITE_CLOSED);
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
