#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "event_loop.c"
#include "sockets.c"


struct proxy_context {
    int eventLoop;
    int src_fd;
    int dst_fd;
    ssize_t address_read_size;
    struct sockaddr_in address;
    struct event_context * src_context;
    struct event_context * dst_context;
};

void close_proxy(struct proxy_context * proxyContext) {
    eventLoopDel(proxyContext->eventLoop, proxyContext->dst_fd);
    eventLoopDel(proxyContext->eventLoop, proxyContext->src_fd);
    close(proxyContext->dst_fd);
    close(proxyContext->src_fd);
    // ignore all event
    proxyContext->src_context->handle_out = NULL;
    proxyContext->src_context->handle_in = NULL;
    proxyContext->dst_context->handle_out = NULL;
    proxyContext->dst_context->handle_in = NULL;
}

// for sendfile do not support socket to socket

void proxy_send(struct proxy_context * proxy_context) {
    char buf[65536];
    ssize_t count = read(proxy_context->src_fd, buf, 65535);
    if (count == 0) {
        close_proxy(proxy_context);
        return;
    }
    if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            close_proxy(proxy_context);
            return;
        }
    }
    write(proxy_context->dst_fd, buf, count);
    printf("proxy send, count=%ld, msg=%s\n", count, buf);
    fflush(stdout);
}

void proxy_read(struct proxy_context * proxy_context) {
    char buf[65536];
    ssize_t count = read(proxy_context->dst_fd, buf, 65535);
    if (count == 0) {
        close_proxy(proxy_context);
        return;
    }
    if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            close_proxy(proxy_context);
            return;
        }
    }
    write(proxy_context->src_fd, buf, count);
    printf("proxy read, count=%ld, msg=%s\n", count, buf);
    fflush(stdout);
}