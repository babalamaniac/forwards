#include <stdlib.h>
#include <unistd.h>

#include "proxy_context.c"

void handle_close(struct event_context * context) {
    do_close(context -> data);
}

void handle_in(struct event_context * context) {
    proxy_handle_in(context -> data);
}

void handle_out(struct event_context * context) {
    proxy_handle_out(context->data);
}

void handle_read_close(struct event_context * context) {
    do_read_close(context -> data);
}

struct event_context * init_proxy_event_context(struct event_context * event_context, struct proxy_context * proxy_context) {
    event_context -> data = proxy_context;
    event_context -> fd = proxy_context -> fd;
    event_context -> handle_in = handle_in;
    event_context -> handle_out = handle_out;
    event_context -> handle_close = handle_close;
    event_context -> handle_read_close = handle_read_close;
    return event_context;
}