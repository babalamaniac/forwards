#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

struct event_context {
    void * data;
    int fd;
    int pipe[2];
    int eventLoop;
    void (*handle_out) (struct event_context * context);
    void (*handle_in)  (struct event_context * context);
    void (*handle_err) (struct event_context * context);
    void (*handle_close) (struct event_context * context);
    void (*handle_read_close) (struct event_context * context);
    int closed;
};

int createEpollEventLoop() {
    return epoll_create(1);
}

struct event_context * initContext() {
    struct event_context *event_context = malloc(sizeof(struct event_context));
    event_context -> handle_err = NULL;
    event_context -> handle_out = NULL;
    event_context -> handle_in = NULL;
    event_context -> closed = 0;
    return event_context;
}

struct event_context * init_event_context(
        void * data,
        int fd,
        void (*handle_in) (struct event_context * context),
        void (*handle_out) (struct event_context * context),
        void (*handle_err) (struct event_context * context),
        void (*handle_read_close) (struct event_context * context)) {
    struct event_context *event_context = malloc(sizeof(struct event_context));
    event_context -> fd = fd;
    event_context -> handle_err = NULL;
    event_context -> handle_out = handle_out;
    event_context -> handle_in = handle_in;
    event_context -> handle_err = handle_err;
    event_context -> handle_close = handle_err;

    event_context -> data = data;
    event_context -> closed = 0;
    return event_context;
}

void eventLoopAdd(int eventLoop, struct event_context * context) {
    struct epoll_event * epoll_event = malloc(sizeof(struct epoll_event));
    epoll_event -> data.ptr = context;
    context -> eventLoop = eventLoop;

    epoll_event -> events = EPOLLET | EPOLLOUT | EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
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

                close(context -> fd);
                eventLoopDel(epollFD, context -> fd);
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

void start_event_loop(int server_fd, void (*handler)  (struct event_context * context)) {
    // init event loop
    int eventLoop = createEpollEventLoop();
    if (eventLoop <= 0) {
        exit(eventLoop);
    }

    // add server fd
    struct event_context server_fd_context;
    server_fd_context.fd = server_fd;
    server_fd_context.handle_in = handler;
    eventLoopAdd(eventLoop, &server_fd_context);
    listen(server_fd_context.fd, 10);

    printf("event loop start\n");
    fflush(stdout);
    mainLoop(eventLoop);
}
