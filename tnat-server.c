#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <linux/netfilter_ipv4.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include "fcntl.h"

#define LISTEN_ADDRESS "192.168.107.5"
#define LISTEN_PORT 12345

int createServerSocket(const char * address, short port) {
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        printf("socket listen failed\n");
        return -1;
    }
    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = inet_addr(address);
    int res = bind(socketfd, (struct sockaddr *)&sockaddr, sizeof(struct sockaddr));
    if (res == -1) {
        printf("socket bind failed\n");
        return -1;
    }
    return socketfd;
}

struct transfer_context {
    int src_fd;
    int dst_fd;
    int dest_ip;
    short dest_port;
    char inited;
};

void transfer(void * data) {
    struct transfer_context * context = (struct transfer_context *) data;
    int size = sendfile(context->src_fd, context->dst_fd, 0, 1024);
    printf("%d\n", size);
}

struct sockaddr_in dest_addr;
socklen_t socklen = sizeof(struct sockaddr_in);

struct event_context {
    void * data;
    int fd;
    int epfd;
    void (*handle_out) (struct event_context * context);
    void (*handle_in)  (struct event_context * context);
    void (*handle_err) (struct event_context * context);
};


void set_fd_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);
}

struct sockaddr remote_proxy_server;

void src_event_handler(void * data, struct epoll_event * event) {
    struct transfer_context * transferContext = (struct transfer_context *) data;
}

// build epoll event context
struct epoll_event * build_event_context(int events, void * ext) {
    struct epoll_event * epoll_event = malloc(sizeof(struct epoll_event));
    epoll_event -> events = events;
    epoll_event -> data.ptr = ext;
    return epoll_event;
}

void client_accept(struct event_context * context) {
    int epfd = context -> epfd;
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 20; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        int dst_fd = socket(AF_INET, SOCK_STREAM, 0);

        // set fd nonblock
        set_fd_nonblock(dst_fd);
        set_fd_nonblock(src_fd);

        // get original dest address
        struct sockaddr_in destaddr;
        getsockopt(src_fd, SOL_IP, SO_ORIGINAL_DST, &destaddr, NULL);

        // build transfer context
        struct transfer_context *transfer_context = malloc(sizeof(struct transfer_context));
        transfer_context->src_fd = src_fd;
        transfer_context->dest_ip = destaddr.sin_addr.s_addr;
        transfer_context->dest_port = destaddr.sin_port;
        transfer_context->dst_fd = dst_fd;
        transfer_context->inited = 0;

        // dst context
        struct event_context *dst_event_context = malloc(sizeof(struct event_context));
        dst_event_context->data = transfer_context;
        dst_event_context->epfd = epfd;
        dst_event_context->handle_out = NULL;
        // src context
        struct event_context *src_event_context = malloc(sizeof(struct event_context));
        src_event_context->data = transfer_context;
        src_event_context->epfd = epfd;

        // add to epoll
        epoll_ctl(epfd, EPOLL_CTL_ADD, dst_fd, build_event_context(EPOLLIN | EPOLLOUT, dst_event_context));
        epoll_ctl(epfd, EPOLL_CTL_ADD, src_fd, build_event_context(EPOLLIN | EPOLLOUT, src_event_context));

        // connect remote proxy async
        connect(dst_fd, &remote_proxy_server, sizeof(struct sockaddr));
    }
}


int main() {
    int epfd = epoll_create(1);
    if (epfd <= 0) {
        exit(epfd);
    }
    int server_fd = createServerSocket(LISTEN_ADDRESS, LISTEN_PORT);
    set_fd_nonblock(server_fd);
    //
    struct event_context server_fd_context;
    server_fd_context.fd = server_fd;
    server_fd_context.epfd = epfd;
    server_fd_context.handle_in = client_accept;

    // add to epoll
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, build_event_context(EPOLLIN | EPOLLERR, &server_fd_context));

    struct epoll_event events[128];
    int event_nums;
    while (1) {
        event_nums = epoll_wait(epfd, events, 128, -1);
		// unexpected +
        if (event_nums < 0) {
            break;
        }
        if (event_nums == 0) {
            continue;
        }
		// unexpected -
		
		
		// 
        while (event_nums -- > 0) {
            struct epoll_event epollEvent = events[event_nums];
            struct event_context * context = (struct event_context *) epollEvent.data.ptr;
            if (epollEvent.events & EPOLLOUT) {
                context -> handle_out(context);
            }
            if (epollEvent.events & EPOLLIN) {
                context -> handle_in(context);
            }
            if (epollEvent.events & EPOLLERR) {
                context -> handle_err(context);
            }
        }
    }
}
