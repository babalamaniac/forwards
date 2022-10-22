#include <stdlib.h>
#include <unistd.h>

#include "proxy_context.c"

void do_close(struct event_context * context) {
    struct proxy_context * proxy_context = context -> data;
    struct proxy_context * peer = proxy_context -> peer;
    if (proxy_need_close(peer)) {
        free(proxy_context);
        free(peer);
    }
}

void unregister_if_need(struct event_context * context) {
    struct proxy_context * proxy_context = context -> data;
    if (proxy_need_close(proxy_context)) {
        do_close(context);
    }
}

void handle_in(struct event_context * context) {
    proxy_handle_in(context -> data);
    unregister_if_need(context);
}

void handle_out(struct event_context * context) {
    proxy_handle_out(context->data);
    unregister_if_need(context);
}

void handle_read_close(struct event_context * context) {
    struct proxy_context * proxy_context = context -> data;
    close(proxy_context -> pipe[1]);
}

void close_proxy(struct event_context * context) {
    printf("close proxy, fd=%d\n", context -> fd);
    struct proxy_context * proxy_context = context -> data;
    proxy_context -> state |= (READ_CLOSED | WRITE_CLOSED);
    handle_in(context);
}

void handle_err(struct event_context * context) {
    struct proxy_context * proxy_context = context -> data;
    proxy_context -> state |= (READ_CLOSED | WRITE_CLOSED);
    close(proxy_context -> pipe[1]);
    close(proxy_context -> fd);
}
