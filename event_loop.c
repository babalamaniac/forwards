#include <stdlib.h>
#include <sys/epoll.h>

struct event_context {
    void * data;
    int fd;
    int eventLoop;
    void (*handle_out) (struct event_context * context);
    void (*handle_in)  (struct event_context * context);
    void (*handle_err) (struct event_context * context);
};

int createEpollEventLoop() {
    return epoll_create(1);
}

struct event_context * initContext() {
    struct event_context *event_context = malloc(sizeof(struct event_context));
    event_context -> handle_err = NULL;
    event_context -> handle_out = NULL;
    event_context -> handle_in = NULL;
    return event_context;
}

void eventLoopAdd(int eventLoop, struct event_context * context) {
    struct epoll_event * epoll_event = malloc(sizeof(struct epoll_event));
    epoll_event -> data.ptr = context;
    context -> eventLoop = eventLoop;

    epoll_event -> events = EPOLLET | EPOLLOUT | EPOLLIN | EPOLLERR;
    epoll_ctl(eventLoop, EPOLL_CTL_ADD, context -> fd, epoll_event);
}

void eventLoopDel(int eventLoop, int fd) {
    // TODO {event} memory leak ?
    epoll_ctl(eventLoop, EPOLL_CTL_DEL, fd, NULL);
}

void mainLoop(int epollFD) {
    struct epoll_event events[128];
    int event_nums;
    while (1) {
        event_nums = epoll_wait(epollFD, events, 128, -1);
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
            if (epollEvent.events & EPOLLOUT && context -> handle_out != NULL) {
                context -> handle_out(context);
            }
            if (epollEvent.events & EPOLLIN && context -> handle_in != NULL) {
                context -> handle_in(context);
            }
            if (epollEvent.events & EPOLLERR && context -> handle_err != NULL) {
                context -> handle_err(context);
            }
        }
    }
}