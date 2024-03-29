#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

#include "sockets.c"

struct event_context {
    void * data;
    int fd;
    int eventLoop;
    void (*handle_out) (struct event_context * context);
    void (*handle_in)  (struct event_context * context);
    void (*handle_err) (struct event_context * context);
    void (*handle_close) (struct event_context * context);
    void (*handle_read_close) (struct event_context * context);
    void (*handle_unregister) (struct event_context * context);
    int closed;
};

void * get_ext(struct event_context * context) {
    return context + 1;
}

int createEpollEventLoop() {
    return epoll_create(1);
}

struct event_context * initContext(size_t ext_size) {
    struct event_context *event_context = malloc(sizeof(struct event_context) + ext_size);
    event_context -> handle_err = NULL;
    event_context -> handle_out = NULL;
    event_context -> handle_in = NULL;
    event_context -> handle_close = NULL;
    event_context -> handle_read_close = NULL;
    event_context -> closed = 0;
    return event_context;
}

void eventLoopAdd(int eventLoop, struct event_context * context) {
    struct epoll_event * epoll_event = malloc(sizeof(struct epoll_event));
    epoll_event -> data.ptr = context;
    context -> eventLoop = eventLoop;

    epoll_event -> events = EPOLLET | EPOLLOUT | EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
    printf("event loop add el=%d, fd=%d, events=%d\n", eventLoop, context -> fd, epoll_event -> events);
    epoll_ctl(eventLoop, EPOLL_CTL_ADD, context -> fd, epoll_event);
}

void eventLoopDel(int eventLoop, int fd) {
    // TODO {event} memory leak ?
    epoll_ctl(eventLoop, EPOLL_CTL_DEL, fd, NULL);
}

void mainLoop(int epollFD) {
    signal(SIGPIPE, SIG_IGN);
    char buf[1024];
    struct epoll_event events[128];
    int event_nums;
    while (1) {
        event_nums = epoll_wait(epollFD, events, 128, -1);
        while (event_nums -- > 0) {
            struct epoll_event epollEvent = events[event_nums];
            struct event_context * context = (struct event_context *) epollEvent.data.ptr;
            if ((epollEvent.events & EPOLLHUP) || (epollEvent.events & EPOLLERR)) {
                printf("handle exception fd=%d, rdhup=%d, hup=%d, err=%d\n", context -> fd, epollEvent.events & EPOLLRDHUP, epollEvent.events & EPOLLHUP, epollEvent.events & EPOLLERR);
                fflush(stdout);
                if (context -> handle_in != NULL) {
                    context -> handle_in(context);
                }
                if (context -> handle_close != NULL) {
                    context -> handle_close(context);
                }

                eventLoopDel(epollFD, context -> fd);
                close(context -> fd);
                free(context);
                continue;
            }
            if ((epollEvent.events & EPOLLIN) && context -> handle_in != NULL) {
                context -> handle_in(context);
            }
            if ((epollEvent.events & EPOLLRDHUP) && context -> handle_read_close != NULL) {
                context -> handle_read_close(context);
            }
            if ((epollEvent.events & EPOLLOUT) && context -> handle_out != NULL) {
                context -> handle_out(context);
            }
        }
    }
}

struct server_context {
    void (*client_init_handler)  (struct event_context * context);
    size_t ext_size;
};

void client_accept(struct event_context * context) {
    struct server_context * server_context = get_ext(context);
    int src_fd = accept(context -> fd, NULL, NULL);
    int i = 0;

    for (;src_fd > 0 && i < 100; i++, src_fd = accept(context -> fd, NULL, NULL)) {
        // get original dest address
        setNonBlock(src_fd);
        printf("accept src fd=%d\n", src_fd);

        struct event_context * client_context = initContext(server_context -> ext_size);
        client_context -> fd = src_fd;
        client_context -> eventLoop = context -> eventLoop;
        server_context -> client_init_handler(client_context);

        eventLoopAdd(context -> eventLoop, client_context);
    }
}

struct event_context * event_connect(int eventLoop, struct sockaddr_in * address, int ext_size) {
    int fd = createSocket();
    struct event_context * context = initContext(ext_size);
    context -> fd = fd;
    eventLoopAdd(eventLoop, context);
    socketConnect(fd, *address);
    return context;
}

void start_event_loop(int server_fd, void (*handler)  (struct event_context * context), size_t ext_size) {
    setNonBlock(server_fd);
    // init event loop
    int eventLoop = createEpollEventLoop();
    if (eventLoop <= 0) {
        exit(eventLoop);
    }

    // add server fd
    struct event_context * server_fd_context = initContext(sizeof(struct server_context));
    server_fd_context -> fd = server_fd;
    server_fd_context -> handle_in = client_accept;

    struct server_context * server_context = get_ext(server_fd_context);
    server_context -> client_init_handler = handler;
    server_context -> ext_size = ext_size;

    eventLoopAdd(eventLoop, server_fd_context);
    listen(server_fd, 100);

    printf("event loop start\n");
    fflush(stdout);
    mainLoop(eventLoop);
}
